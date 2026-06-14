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
        // Reference the cached native routing ids instead of copying the
        // 256-byte ZlinkRoutingId structs, mirroring SocketKernel routed send.
        ZlinkRoutingId nodeFallback = default;
        ZlinkRoutingId spotFallback = default;
        ref ZlinkRoutingId nodeRid = ref destNodeRid.ToNativeRef(ref nodeFallback);
        ref ZlinkRoutingId spotRid = ref destSpotRid.ToNativeRef(ref spotFallback);
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
