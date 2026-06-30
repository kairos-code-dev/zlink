// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed partial class Spot : ISpot
{
    internal async Task<IReadOnlyList<Message>> RequestToChannelAsync(string channelName,
        Message message, TimeSpan timeout = default, CancellationToken ct = default)
    {
        ValidateChannelName(channelName, nameof(channelName));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        var received = await RequestToChannelAsyncInternal(channelName,
            new[] { message }, timeout, ct).ConfigureAwait(false);
        return received.Parts;
    }

    internal async Task<IReadOnlyList<Message>> RequestToChannelAsync(string channelName,
        IReadOnlyList<Message> parts, TimeSpan timeout = default,
        CancellationToken ct = default)
    {
        ValidateChannelName(channelName, nameof(channelName));
        var received = await RequestToChannelAsyncInternal(channelName,
            parts, timeout, ct).ConfigureAwait(false);
        return received.Parts;
    }

    internal bool RequestToChannel(string channelName, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan? timeout = null)
    {
        return RequestToChannel(channelName, message, callback, SendFlags.None, timeout);
    }

    internal bool RequestToChannel(string channelName, IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan? timeout = null)
    {
        return RequestToChannel(channelName, parts, callback, SendFlags.None, timeout);
    }

    internal bool RequestToChannel(string channelName, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags, TimeSpan? timeout = null)
    {
        return RequestToChannel(channelName, new[] { message }, callback, flags,
            timeout);
    }

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
                return false;

            throw;
        }
    }

    private unsafe Task<Received> RequestToChannelAsyncInternal(string channelName,
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct,
        int flags = 0)
    {
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        var cloned = RequestReplySupport.CloneParts(parts);
        var channelNameUtf8 = GetChannelNameUtf8(channelName);
        var timeoutMs = RequestReplySupport.NormalizeTimeout(timeout);
        var completion = new TaskCompletionSource<Received>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        RequestCallState? state = null;

        try
        {
            state = new RequestCallState(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            if (ct.CanBeCanceled)
                state.SetCancellationRegistration(
                    ct.Register(static userdata => { RequestCallState.CancelFromUserData(userdata); }, handle));

            state.SetTimeoutTimer(new System.Threading.Timer(
                static userdata => { RequestCallState.TimeoutFromUserData(userdata); }, handle, (int)timeoutMs,
                Timeout.Infinite));

            fixed (byte* channelPtr = channelNameUtf8)
            {
                for (var i = 0; i < cloned.Length; i++)
                {
                    ZlinkMsg nativePart = default;
                    cloned[i].MoveTo(ref nativePart);
                    var submitted = false;
                    try
                    {
                        var rc = NativeMethods.zlink_spot_request_channel_part_utf8(
                            Handle, channelPtr, ref nativePart,
                            RoutedReplyHandlerPointer,
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

            return RequestProgressPump.AttachSpot(Handle, completion.Task);
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