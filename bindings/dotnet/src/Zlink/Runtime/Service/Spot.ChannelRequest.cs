// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Runtime.Native;
using Systems.Zlink.Runtime.Sockets.Internal;

namespace Systems.Zlink;

internal sealed partial class Spot : ISpot
{
    internal async Task<IReadOnlyList<Message>> RequestToChannelAsync(string channelName,
        Message message, TimeSpan timeout = default, CancellationToken ct = default)
    {
        ValidateChannelName(channelName, nameof(channelName));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        Received received = await RequestToChannelAsyncInternal(channelName,
            new[] { message }, timeout, ct).ConfigureAwait(false);
        return received.Parts;
    }

    internal async Task<IReadOnlyList<Message>> RequestToChannelAsync(string channelName,
        IReadOnlyList<Message> parts, TimeSpan timeout = default,
        CancellationToken ct = default)
    {
        ValidateChannelName(channelName, nameof(channelName));
        Received received = await RequestToChannelAsyncInternal(channelName,
            parts, timeout, ct).ConfigureAwait(false);
        return received.Parts;
    }

    internal bool RequestToChannel(string channelName, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan? timeout = null)
        => RequestToChannel(channelName, message, callback, SendFlags.None, timeout);

    internal bool RequestToChannel(string channelName, IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan? timeout = null)
        => RequestToChannel(channelName, parts, callback, SendFlags.None, timeout);

    internal bool RequestToChannel(string channelName, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags, TimeSpan? timeout = null)
        => RequestToChannel(channelName, new[] { message }, callback, flags,
            timeout);

    internal bool RequestToChannel(string channelName, IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags, TimeSpan? timeout = null)
    {
        ValidateChannelName(channelName, nameof(channelName));
        try
        {
            RequestReplySupport.AttachResultCallback(
                () => RequestToChannelAsyncInternal(channelName, parts,
                    timeout ?? TimeSpan.Zero,
                    CancellationToken.None, (int)flags),
                (result, reply) =>
                {
                    IReadOnlyList<Message> payload = Array.Empty<Message>();
                    if (reply != null)
                    {
                        payload = RequestReplySupport.TakeOwnedParts(reply);
                        reply.Dispose();
                    }
                    callback(result, payload);
                });
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0)
        {
            if (RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
            {
                return false;
            }

            throw;
        }
    }

    private unsafe Task<Received> RequestToChannelAsyncInternal(string channelName,
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct,
        int flags = 0)
    {
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        byte[] channelNameUtf8 = GetChannelNameUtf8(channelName);
        uint timeoutMs = RequestReplySupport.NormalizeTimeout(timeout);
        var completion = new TaskCompletionSource<Received>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        RequestCallState? state = null;

        try
        {
            state = new RequestCallState(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            if (ct.CanBeCanceled)
            {
                state.SetCancellationRegistration(ct.Register(static userdata =>
                {
                    RequestCallState.CancelFromUserData(userdata);
                }, handle));
            }

            state.SetTimeoutTimer(new System.Threading.Timer(static userdata =>
            {
                RequestCallState.TimeoutFromUserData(userdata);
            }, handle, (int)timeoutMs, Timeout.Infinite));

            fixed (byte* channelPtr = channelNameUtf8)
            {
                for (int i = 0; i < cloned.Length; i++)
                {
                    ZlinkMsg nativePart = default;
                    cloned[i].MoveTo(ref nativePart);
                    bool submitted = false;
                    try
                    {
                        int rc = NativeMethods.zlink_spot_request_channel_part_utf8(
                            _handle, channelPtr, ref nativePart,
                            RoutedReplyHandlerPtr,
                            GCHandle.ToIntPtr(handle),
                            flags,
                            i + 1 < cloned.Length
                                ? NativeMethods.ZlinkPartFlag.More
                                : NativeMethods.ZlinkPartFlag.Final,
                            timeoutMs);
                        submitted = true;
                        if (rc != 0)
                            throw ZlinkException.CreateSubmitException(
                                NativeMethods.zlink_errno());
                    }
                    finally
                    {
                        if (!submitted)
                            NativeMethods.zlink_msg_close(ref nativePart);
                    }
                }
            }

            return RequestProgressPump.AttachSpot(_handle, completion.Task);
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }
}
