// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink.Runtime.Sockets.Internal;

internal sealed partial class SocketKernel : IDisposable
{
    internal bool SendReceivedSingle(RoutingIdSnapshot routingId,
        RoutingIdSnapshot spotRid, Message part, SendFlags flags)
    {
        if (part == null)
            throw new ArgumentNullException(nameof(part));
        if (!routingId.HasValue)
            throw new ZlinkSubmitException(SubmitResult.InvalidArgument,
                (int)ErrorCode.EInval);

        var targetSpot = spotRid.ToRoutingId();
        if (targetSpot.HasValue)
        {
            var target = routingId.ToRoutingId();
            if (!target.HasValue)
                throw new ZlinkSubmitException(SubmitResult.InvalidArgument,
                    (int)ErrorCode.EInval);
            return SendSingleToSpotCore(target.Value, targetSpot.Value, part,
                flags);
        }

        ZlinkRoutingId nativeRoutingId = default;
        routingId.WriteNative(ref nativeRoutingId);
        if ((flags & SendFlags.DontWait) != 0)
            return SendSingleResultCore(ref nativeRoutingId, part,
                (int)flags) == SendResult.Sent;

        SendSingleCore(ref nativeRoutingId, part, (int)flags);
        return true;
    }

    internal bool SendReceivedParts(RoutingIdSnapshot routingId,
        RoutingIdSnapshot spotRid, IReadOnlyList<Message> parts, SendFlags flags)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));

        var target = routingId.ToRoutingId();
        if (!target.HasValue)
            throw new ZlinkSubmitException(SubmitResult.InvalidArgument,
                (int)ErrorCode.EInval);

        var targetSpot = spotRid.ToRoutingId();
        if (targetSpot.HasValue)
            return SendToSpotCore(target.Value, targetSpot.Value, parts, flags);

        if ((flags & SendFlags.DontWait) != 0)
            return SendNoWaitResult(target.Value, parts) == SendResult.Sent;

        Send(target.Value, parts, flags);
        return true;
    }

    private ReceivedSendHandler? CreateRoutedSendHandler(
        RoutingIdSnapshot routingId, RoutingIdSnapshot spotRid)
    {
        var target = routingId.ToRoutingId();
        if (target == null)
            return null;
        var targetSpot = spotRid.ToRoutingId();
        if (targetSpot.HasValue)
        {
            var nodeRid = target.Value;
            var spotTarget = targetSpot.Value;
            return (sendParts, sendFlags) => SendToSpotCore(nodeRid, spotTarget,
                sendParts, sendFlags);
        }

        return (sendParts, sendFlags) =>
        {
            if ((sendFlags & SendFlags.DontWait) != 0)
                return SendNoWaitResult(target.Value, sendParts) == SendResult.Sent;
            Send(target.Value, sendParts, sendFlags);
            return true;
        };
    }

    private ReceivedSendSingleHandler? CreateRoutedSendSingleHandler(
        RoutingIdSnapshot routingId, RoutingIdSnapshot spotRid)
    {
        if (!routingId.HasValue)
            return null;
        var targetSpot = spotRid.ToRoutingId();
        if (targetSpot.HasValue)
        {
            var targetNode = routingId.ToRoutingId();
            if (!targetNode.HasValue)
                return null;
            var nodeRid = targetNode.Value;
            var spotTarget = targetSpot.Value;
            return (sendPart, sendFlags) => SendSingleToSpotCore(nodeRid,
                spotTarget, sendPart, sendFlags);
        }

        var target = routingId.ToRoutingId();
        if (!target.HasValue)
            return null;
        var targetRid = target.Value;
        return (sendPart, sendFlags) =>
        {
            if ((sendFlags & SendFlags.DontWait) != 0)
                return SendRoutedMessageResultUnchecked(targetRid, sendPart,
                           (int)sendFlags)
                       == SendResult.Sent;
            SendRoutedMessageUnchecked(targetRid, sendPart, sendFlags);
            return true;
        };
    }

    private bool SendToSpotCore(RoutingId destNodeRid,
        RoutingId destSpotRid, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        var nodeRid = destNodeRid.ToNative();
        var spotRid = destSpotRid.ToNative();
        var cloned = RequestReplySupport.CloneParts(parts);
        try
        {
            RequestReplySupport.SubmitClonedParts(cloned,
                (ref ZlinkMsg nativePart,
                        NativeMethods.ZlinkPartFlag partFlag) =>
                    NativeMethods.zlink_router_send_spot_part(Handle,
                        ref nodeRid, ref spotRid, ref nativePart, (int)flags,
                        partFlag));
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
                                           && RequestReplySupport.MapSendNoWaitResult(error)
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

    private bool SendSingleToSpotCore(RoutingId destNodeRid,
        RoutingId destSpotRid, Message part, SendFlags flags)
    {
        if (part == null)
            throw new ArgumentNullException(nameof(part));

        var nodeRid = destNodeRid.ToNative();
        var spotRid = destSpotRid.ToNative();
        // Preserve SendToSpotCore ownership semantics without the temporary
        // IReadOnlyList wrapper used by the multipart path.
        var cloned = part.Copy();
        ZlinkMsg nativePart = default;
        var submitted = false;
        try
        {
            cloned.MoveTo(ref nativePart);
            var rc = NativeMethods.zlink_router_send_spot_part(Handle,
                ref nodeRid, ref spotRid, ref nativePart, (int)flags,
                NativeMethods.ZlinkPartFlag.Final);
            submitted = true;
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
                                           && RequestReplySupport.MapSendNoWaitResult(error)
                                           == SendResult.Backpressured)
        {
            cloned.Dispose();
            return false;
        }
        catch
        {
            cloned.Dispose();
            throw;
        }
        finally
        {
            if (!submitted)
                NativeMethods.zlink_msg_close(ref nativePart);
        }
    }
}
