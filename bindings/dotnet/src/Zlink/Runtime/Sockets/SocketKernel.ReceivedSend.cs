// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
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
        {
            throw new ZlinkSubmitException(SubmitResult.InvalidArgument,
                (int)ErrorCode.EInval);
        }

        RoutingId? targetSpot = spotRid.ToRoutingId();
        if (targetSpot.HasValue)
        {
            RoutingId? target = routingId.ToRoutingId();
            if (!target.HasValue)
            {
                throw new ZlinkSubmitException(SubmitResult.InvalidArgument,
                    (int)ErrorCode.EInval);
            }
            return SendSingleToSpotCore(target.Value, targetSpot.Value, part,
                flags);
        }

        ZlinkRoutingId nativeRoutingId = default;
        routingId.WriteNative(ref nativeRoutingId);
        if ((flags & SendFlags.DontWait) != 0)
        {
            return SendSingleResultCore(ref nativeRoutingId, part,
                (int)flags) == SendResult.Sent;
        }

        SendSingleCore(ref nativeRoutingId, part, (int)flags);
        return true;
    }

    internal bool SendReceivedParts(RoutingIdSnapshot routingId,
        RoutingIdSnapshot spotRid, IReadOnlyList<Message> parts, SendFlags flags)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));

        RoutingId? target = routingId.ToRoutingId();
        if (!target.HasValue)
        {
            throw new ZlinkSubmitException(SubmitResult.InvalidArgument,
                (int)ErrorCode.EInval);
        }

        RoutingId? targetSpot = spotRid.ToRoutingId();
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
        RoutingId? target = routingId.ToRoutingId();
        if (target == null)
            return null;
        RoutingId? targetSpot = spotRid.ToRoutingId();
        if (targetSpot.HasValue)
        {
            RoutingId nodeRid = target.Value;
            RoutingId spotTarget = targetSpot.Value;
            return (sendParts, sendFlags) => SendToSpotCore(nodeRid, spotTarget,
                sendParts, sendFlags);
        }
        return (sendParts, sendFlags) =>
        {
            if ((sendFlags & SendFlags.DontWait) != 0)
            {
                return SendNoWaitResult(target.Value, sendParts) == SendResult.Sent;
            }
            Send(target.Value, sendParts, sendFlags);
            return true;
        };
    }

    private unsafe ReceivedSendSingleHandler? CreateRoutedSendSingleHandler(
        RoutingIdSnapshot routingId, RoutingIdSnapshot spotRid)
    {
        if (!routingId.HasValue)
            return null;
        RoutingId? targetSpot = spotRid.ToRoutingId();
        if (targetSpot.HasValue)
        {
            RoutingId? targetNode = routingId.ToRoutingId();
            if (!targetNode.HasValue)
                return null;
            RoutingId nodeRid = targetNode.Value;
            RoutingId spotTarget = targetSpot.Value;
            return (sendPart, sendFlags) => SendSingleToSpotCore(nodeRid,
                spotTarget, sendPart, sendFlags);
        }

        RoutingId? target = routingId.ToRoutingId();
        if (!target.HasValue)
            return null;
        RoutingId targetRid = target.Value;
        return (sendPart, sendFlags) =>
        {
            if ((sendFlags & SendFlags.DontWait) != 0)
            {
                return SendRoutedMessageResultUnchecked(targetRid, sendPart,
                    (int)sendFlags)
                    == SendResult.Sent;
            }
            SendRoutedMessageUnchecked(targetRid, sendPart, sendFlags);
            return true;
        };
    }

    private unsafe bool SendToSpotCore(RoutingId destNodeRid,
        RoutingId destSpotRid, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        ZlinkRoutingId nodeRid = destNodeRid.ToNative();
        ZlinkRoutingId spotRid = destSpotRid.ToNative();
        Message[] cloned = RequestReplySupport.CloneParts(parts);
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

    private unsafe bool SendSingleToSpotCore(RoutingId destNodeRid,
        RoutingId destSpotRid, Message part, SendFlags flags)
    {
        if (part == null)
            throw new ArgumentNullException(nameof(part));

        ZlinkRoutingId nodeRid = destNodeRid.ToNative();
        ZlinkRoutingId spotRid = destSpotRid.ToNative();
        // Preserve SendToSpotCore ownership semantics without the temporary
        // IReadOnlyList wrapper used by the multipart path.
        Message cloned = RequestReplySupport.CloneMessage(part);
        ZlinkMsg nativePart = default;
        bool submitted = false;
        try
        {
            cloned.MoveTo(ref nativePart);
            int rc = NativeMethods.zlink_router_send_spot_part(Handle,
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
