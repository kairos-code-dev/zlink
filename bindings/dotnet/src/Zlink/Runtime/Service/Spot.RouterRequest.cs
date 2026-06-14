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
        // Reference the cached native routing ids rather than copying the
        // 256-byte structs; parts are submitted synchronously in the loop
        // below so the refs stay valid (no closure capture here).
        ZlinkRoutingId nodeFallback = default;
        ZlinkRoutingId spotFallback = default;
        ref ZlinkRoutingId nodeRid = ref destNodeRid.ToNativeRef(ref nodeFallback);
        ref ZlinkRoutingId spotRid = ref destSpotRid.ToNativeRef(ref spotFallback);
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
}
