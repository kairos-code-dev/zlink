// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed partial class Spot : ISpot
{
    internal void ReplyToRouter(RoutingId peerRid, ulong requestSeq,
        Message message, SendFlags flags = SendFlags.None)
    {
        ReplyToRouter(peerRid, requestSeq, new[] { message }, flags);
    }

    internal Task<IReadOnlyList<Message>> RequestToRouterAsync(RoutingId peerRid,
        Message message, TimeSpan timeout = default,
        CancellationToken ct = default)
    {
        return RequestToRouterAsync(peerRid, new[] { message }, timeout, ct);
    }

    internal async Task<IReadOnlyList<Message>> RequestToRouterAsync(RoutingId peerRid,
        IReadOnlyList<Message> parts, TimeSpan timeout = default,
        CancellationToken ct = default)
    {
        var received = await RequestToRouterAsyncInternal(peerRid, parts,
            timeout, ct).ConfigureAwait(false);
        return received.Parts;
    }

    internal bool RequestToRouter(RoutingId peerRid, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
    {
        return RequestToRouter(peerRid, new[] { message }, callback, flags,
            timeout);
    }

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

    internal void ReplyToRouter(RoutingId peerRid, ulong requestSeq,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None)
    {
        _ = flags;
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        var routingId = peerRid.ToNative();
        var cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            RequestReplySupport.SubmitClonedParts(cloned,
                (ref ZlinkMsg nativePart,
                        NativeMethods.ZlinkPartFlag partFlag) =>
                    NativeMethods.zlink_spot_reply_router_part(Handle,
                        ref routingId, requestSeq, ref nativePart, partFlag));
        }
        catch
        {
            RequestReplySupport.DisposeParts(cloned);
            throw;
        }
    }

    private Task<Received> RequestToSpotAsyncInternal(
        RoutingId destNodeRid, RoutingId destSpotRid, IReadOnlyList<Message> parts,
        TimeSpan timeout, CancellationToken ct, int flags = 0)
    {
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        var nodeRid = destNodeRid.ToNative();
        var spotRid = destSpotRid.ToNative();
        return RequestRoutedAsyncInternal(parts, timeout, ct, flags,
            (ref ZlinkMsg nativePart, IntPtr handler, IntPtr userData,
                    NativeMethods.ZlinkPartFlag partFlag, uint timeoutMs) =>
                NativeMethods.zlink_spot_request_spot_part(Handle,
                    ref nodeRid, ref spotRid, ref nativePart, handler, userData,
                    flags, partFlag, timeoutMs));
    }

    private bool RequestToSpotCallbackInternal(
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
        ref var nodeRid = ref destNodeRid.ToNativeRef(ref nodeFallback);
        ref var spotRid = ref destSpotRid.ToNativeRef(ref spotFallback);
        var timeoutMs = RequestReplySupport.NormalizeTimeout(timeout);
        var cloned = RequestReplySupport.CloneParts(parts);
        GCHandle handle = default;
        SpotRequestCallbackState? state = null;

        try
        {
            state = new SpotRequestCallbackState(callback,
                RequestProgressPump.AttachSpotCallback(Handle));
            handle = GCHandle.Alloc(state, GCHandleType.Normal);

            for (var i = 0; i < cloned.Length; i++)
            {
                ZlinkMsg nativePart = default;
                cloned[i].MoveTo(ref nativePart);
                var submitted = false;
                try
                {
                    var rc = NativeMethods.zlink_spot_request_spot_part(Handle,
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

    private Task<Received> RequestToRouterAsyncInternal(RoutingId peerRid,
        IReadOnlyList<Message> parts, TimeSpan timeout, CancellationToken ct,
        int flags = 0)
    {
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        var nativePeerRid = peerRid.ToNative();
        return RequestRoutedAsyncInternal(parts, timeout, ct, flags,
            (ref ZlinkMsg nativePart, IntPtr handler, IntPtr userData,
                    NativeMethods.ZlinkPartFlag partFlag, uint timeoutMs) =>
                NativeMethods.zlink_spot_request_router_part(Handle,
                    ref nativePeerRid, ref nativePart, handler, userData,
                    flags, partFlag, timeoutMs));
    }
}