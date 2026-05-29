// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Native;
using Systems.Zlink.Sockets.Internal;

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

    internal void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSeq, Message message, SendFlags flags = SendFlags.None)
        => ReplyToSpot(destNodeRid, destSpotRid, requestSeq, new[] { message },
            flags);

    internal Task<IReadOnlyList<Message>> RequestToSpotAsync(
        RoutingId destNodeRid, RoutingId destSpotRid, Message message,
        TimeSpan timeout = default, CancellationToken ct = default)
        => RequestToSpotAsync(destNodeRid, destSpotRid, new[] { message },
            timeout, ct);

    internal async Task<IReadOnlyList<Message>> RequestToSpotAsync(
        RoutingId destNodeRid, RoutingId destSpotRid, IReadOnlyList<Message> parts,
        TimeSpan timeout = default, CancellationToken ct = default)
    {
        Received received = await RequestToSpotAsyncInternal(destNodeRid,
            destSpotRid, parts, timeout, ct).ConfigureAwait(false);
        return received.Parts;
    }

    internal bool RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        Message message, Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
        => RequestToSpot(destNodeRid, destSpotRid, new[] { message }, callback,
            flags, timeout);

    internal bool RequestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        try
        {
            return RequestToSpotCallbackInternal(destNodeRid, destSpotRid, parts,
                callback, flags, timeout ?? TimeSpan.Zero);
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
        {
            return false;
        }
    }

    internal bool SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        Message message, SendFlags flags = SendFlags.None)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        ZlinkRoutingId nodeRid = destNodeRid.ToNative();
        ZlinkRoutingId spotRid = destSpotRid.ToNative();
        try
        {
            int rc = SubmitCopiedSpotSendSingle(ref nodeRid, ref spotRid,
                message, (int)flags);
            if (rc == 0)
                return true;
            throw ZlinkException.CreateSubmitException(NativeMethods.zlink_errno());
        }
        catch (ZlinkException ex) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(ex)
                == SendResult.Backpressured)
        {
            return false;
        }
    }

    internal unsafe bool SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        ZlinkRoutingId nodeRid = destNodeRid.ToNative();
        ZlinkRoutingId spotRid = destSpotRid.ToNative();
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            RequestReplySupport.SubmitClonedParts(cloned,
                (ref ZlinkMsg nativePart,
                    NativeMethods.ZlinkPartFlag partFlag) =>
                    NativeMethods.zlink_spot_send_spot_part(_handle,
                        ref nodeRid, ref spotRid, ref nativePart, (int)flags,
                        partFlag));
            return true;
        }
        catch (ZlinkException ex) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(ex)
                == SendResult.Backpressured)
        {
            RequestReplySupport.DisposeParts(cloned);
            return false;
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    private unsafe int SubmitCopiedSpotSendSingle(
        ref ZlinkRoutingId nodeRid, ref ZlinkRoutingId spotRid, Message source,
        int flags)
    {
        ZlinkMsg nativePart = default;
        bool submitted = false;
        source.CopyTo(ref nativePart);
        try
        {
            int rc = NativeMethods.zlink_spot_send_spot_part(_handle,
                ref nodeRid, ref spotRid, ref nativePart, flags,
                NativeMethods.ZlinkPartFlag.Final);
            submitted = true;
            return rc;
        }
        finally
        {
            if (!submitted)
                NativeMethods.zlink_msg_close(ref nativePart);
        }
    }

    internal unsafe void ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSeq, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        _ = flags;
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        ZlinkRoutingId nodeRid = destNodeRid.ToNative();
        ZlinkRoutingId spotRid = destSpotRid.ToNative();
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            RequestReplySupport.SubmitClonedParts(cloned,
                (ref ZlinkMsg nativePart,
                    NativeMethods.ZlinkPartFlag partFlag) =>
                    NativeMethods.zlink_spot_reply_spot_part(_handle,
                        ref nodeRid, ref spotRid, requestSeq, ref nativePart,
                        partFlag));
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    internal void ReplyToRouter(RoutingId peerRid, ulong requestSeq,
        Message message, SendFlags flags = SendFlags.None)
        => ReplyToRouter(peerRid, requestSeq, new[] { message }, flags);

    internal Task<IReadOnlyList<Message>> RequestToRouterAsync(RoutingId peerRid,
        Message message, TimeSpan timeout = default,
        CancellationToken ct = default)
        => RequestToRouterAsync(peerRid, new[] { message }, timeout, ct);

    internal async Task<IReadOnlyList<Message>> RequestToRouterAsync(RoutingId peerRid,
        IReadOnlyList<Message> parts, TimeSpan timeout = default,
        CancellationToken ct = default)
    {
        Received received = await RequestToRouterAsyncInternal(peerRid, parts,
            timeout, ct).ConfigureAwait(false);
        return received.Parts;
    }

    internal bool RequestToRouter(RoutingId peerRid, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
        => RequestToRouter(peerRid, new[] { message }, callback, flags,
            timeout);

    internal bool RequestToRouter(RoutingId peerRid, IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        try
        {
            RequestReplySupport.AttachResultCallback(
                () => RequestToRouterAsyncInternal(peerRid, parts,
                    timeout ?? TimeSpan.Zero, CancellationToken.None, (int)flags),
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
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
        {
            return false;
        }
    }

    internal unsafe void ReplyToRouter(RoutingId peerRid, ulong requestSeq,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        _ = flags;
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        ZlinkRoutingId routingId = peerRid.ToNative();
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            RequestReplySupport.SubmitClonedParts(cloned,
                (ref ZlinkMsg nativePart,
                    NativeMethods.ZlinkPartFlag partFlag) =>
                    NativeMethods.zlink_spot_reply_router_part(_handle,
                        ref routingId, requestSeq, ref nativePart, partFlag));
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    private unsafe Task<Received> RequestToChannelAsyncInternal(string channelName,
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct,
        int flags = 0)
    {
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        Message[] cloned = RequestReplySupport.CloneParts(parts);
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

            for (int i = 0; i < cloned.Length; i++)
            {
                ZlinkMsg nativePart = default;
                cloned[i].MoveTo(ref nativePart);
                bool submitted = false;
                try
                {
                    int rc = NativeMethods.zlink_spot_request_channel_part(_handle,
                        channelName, ref nativePart,
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

    private unsafe Task<Received> RequestToSpotAsyncInternal(
        RoutingId destNodeRid, RoutingId destSpotRid, IReadOnlyList<Message> parts,
        TimeSpan timeout, CancellationToken ct, int flags = 0)
    {
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        ZlinkRoutingId nodeRid = destNodeRid.ToNative();
        ZlinkRoutingId spotRid = destSpotRid.ToNative();
        return RequestRoutedAsyncInternal(parts, timeout, ct, flags,
            (ref ZlinkMsg nativePart, IntPtr handler, IntPtr userData,
                NativeMethods.ZlinkPartFlag partFlag, uint timeoutMs) =>
                NativeMethods.zlink_spot_request_spot_part(_handle,
                    ref nodeRid, ref spotRid, ref nativePart, handler, userData,
                    flags, partFlag, timeoutMs));
    }

    private unsafe bool RequestToSpotCallbackInternal(
        RoutingId destNodeRid, RoutingId destSpotRid, IReadOnlyList<Message> parts,
        Action<RequestResult, IReadOnlyList<Message>> callback, SendFlags flags,
        TimeSpan timeout)
    {
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        ZlinkRoutingId nodeRid = destNodeRid.ToNative();
        ZlinkRoutingId spotRid = destSpotRid.ToNative();
        uint timeoutMs = RequestReplySupport.NormalizeTimeout(timeout);
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        GCHandle handle = default;
        SpotRequestCallbackState? state = null;

        try
        {
            state = new SpotRequestCallbackState(callback,
                RequestProgressPump.AttachSpotCallback(_handle));
            handle = GCHandle.Alloc(state, GCHandleType.Normal);

            for (int i = 0; i < cloned.Length; i++)
            {
                ZlinkMsg nativePart = default;
                cloned[i].MoveTo(ref nativePart);
                bool submitted = false;
                try
                {
                    int rc = NativeMethods.zlink_spot_request_spot_part(_handle,
                        ref nodeRid, ref spotRid, ref nativePart,
                        RoutedReplyCallbackHandlerPtr, GCHandle.ToIntPtr(handle),
                        (int)flags,
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

            return true;
        }
        catch
        {
            state?.DisposeProgress();
            if (handle.IsAllocated)
                handle.Free();
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    private unsafe Task<Received> RequestToRouterAsyncInternal(RoutingId peerRid,
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct,
        int flags = 0)
    {
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        ZlinkRoutingId nativePeerRid = peerRid.ToNative();
        return RequestRoutedAsyncInternal(parts, timeout, ct, flags,
            (ref ZlinkMsg nativePart, IntPtr handler, IntPtr userData,
                NativeMethods.ZlinkPartFlag partFlag, uint timeoutMs) =>
                NativeMethods.zlink_spot_request_router_part(_handle,
                    ref nativePeerRid, ref nativePart, handler, userData,
                    flags, partFlag, timeoutMs));
    }

    private unsafe delegate int SpotRequestPartSubmitter(
        ref ZlinkMsg nativePart, IntPtr handler, IntPtr userData,
        NativeMethods.ZlinkPartFlag partFlag, uint timeoutMs);

    private unsafe Task<Received> RequestRoutedAsyncInternal(
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct,
        int flags, SpotRequestPartSubmitter submit)
    {
        Message[] cloned = RequestReplySupport.CloneParts(parts);
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

            for (int i = 0; i < cloned.Length; i++)
            {
                ZlinkMsg nativePart = default;
                cloned[i].MoveTo(ref nativePart);
                bool submitted = false;
                try
                {
                    int rc = submit(ref nativePart,
                        RoutedReplyHandlerPtr,
                        GCHandle.ToIntPtr(handle),
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

            return RequestProgressPump.AttachSpot(_handle, completion.Task);
        }
        catch
        {
            if (state != null)
                state.Dispose();
            if (handle.IsAllocated)
                handle.Free();
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    private static void OnRoutedReply(int result, IntPtr parts, nuint partCount,
        IntPtr userData)
    {
        RequestReplySupport.CompleteReceivedReply(result, parts, partCount,
            userData);
    }
}
