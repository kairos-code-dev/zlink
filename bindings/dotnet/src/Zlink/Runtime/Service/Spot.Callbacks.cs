// SPDX-License-Identifier: MPL-2.0

using System;
using Systems.Zlink.Native;

namespace Systems.Zlink;

public sealed partial class Spot
{
    private unsafe void OnNativeRoutedReceive(ZlinkRoutingId* sourceRoutingId,
        ZlinkRoutingId* spotRoutingId, ulong requestSeq, IntPtr parts,
        nuint partCount, IntPtr userData)
    {
        Action<Received>? handler = _routedReceiveHandler;
        if (handler == null)
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
            return;
        }

        Message[] managedParts = Message.FromNativeVector(parts, partCount);
        parts = IntPtr.Zero;
        partCount = 0;
        RoutingId? nodeRid = sourceRoutingId == null ? null :
            RoutingIdCodec.ToRoutingId(
                NativeHelpers.ReadRoutingId(ref *sourceRoutingId));
        RoutingId? spotRid = spotRoutingId == null ? null :
            RoutingIdCodec.ToRoutingId(
                NativeHelpers.ReadRoutingId(ref *spotRoutingId));
        Received received = requestSeq == 0
            ? Received.Create(nodeRid, managedParts, spotRid: spotRid)
            : Received.Create(nodeRid, managedParts, requestSeq, spotRid,
                (replyParts, sendFlags) => ReplyToSpot(
                    nodeRid ?? throw new ZlinkSubmitException(
                        SubmitResult.InvalidArgument, (int)ErrorCode.EInval),
                    spotRid ?? throw new ZlinkSubmitException(
                        SubmitResult.InvalidArgument, (int)ErrorCode.EInval),
                    requestSeq, replyParts, sendFlags));
        received.SetSendHandler(CreateRoutedSendHandler(nodeRid, spotRid));
        CallbackDelivery.Post(_routedReceiveHandlerContext, () => handler(received));
    }

    private ReceivedSendHandler? CreateRoutedSendHandler(RoutingId? nodeRid,
        RoutingId? spotRid)
    {
        if (!nodeRid.HasValue || !spotRid.HasValue)
            return null;
        RoutingId targetNode = nodeRid.Value;
        RoutingId targetSpot = spotRid.Value;
        return (sendParts, sendFlags) => SendToSpot(targetNode, targetSpot,
            sendParts, sendFlags);
    }

    private unsafe void OnNativeDispatchEvent(IntPtr spot,
        ZlinkSpotDispatchInfoNative* info, IntPtr userData)
    {
        if (info == null)
            return;

        SpotDispatchEvent eventKind = (SpotDispatchEvent)info->Event;
        SpotDispatchSubjectKind subjectKind =
            (SpotDispatchSubjectKind)info->SubjectKind;
        if (eventKind == SpotDispatchEvent.SubscribeReadable
            && subjectKind == SpotDispatchSubjectKind.Spot)
        {
            Action<SpotDispatchInfo>? dispatchHandler = _dispatchEventHandler;
            if (dispatchHandler == null)
                return;

            try
            {
                dispatchHandler(SpotDispatchInfo.SubscribeReadableSpot);
            }
            catch (Exception ex)
            {
                Runtime.ReportUnhandledCallbackException(ex);
            }
            return;
        }

        Action<SpotDispatchInfo>? handler = _dispatchEventHandler;
        if (handler == null)
            return;

        ActorPart[]? actorParts = eventKind == SpotDispatchEvent.ActorReadable
            && subjectKind == SpotDispatchSubjectKind.Actor
            && info->Subject != IntPtr.Zero
                ? ActorInterop.DrainActorParts(_node.Handle, info->Subject)
                : null;
        Timer? timer = eventKind == SpotDispatchEvent.TimerReadable
            && subjectKind == SpotDispatchSubjectKind.Timer
                ? Timer.FromDispatchSubject(info->Subject)
                : null;
        IntPtr channelDealerSubject = eventKind
            == SpotDispatchEvent.ChannelReplyReadable
            && subjectKind == SpotDispatchSubjectKind.ChannelDealer
                ? info->Subject
                : IntPtr.Zero;
        SpotDispatchInfo dispatchInfo = new(eventKind, subjectKind,
            timer, channelDealerSubject, DrainChannelReplyFrom, actorParts);
        try
        {
            handler(dispatchInfo);
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
        }
    }

    private unsafe void OnNativeActorJoin(IntPtr spot,
        ZlinkSpotActorLifecycleInfo* info, IntPtr userData)
    {
        DispatchActorLifecycle(_actorJoinHandler, info);
    }

    private unsafe void OnNativeActorLeave(IntPtr spot,
        ZlinkSpotActorLifecycleInfo* info, IntPtr userData)
    {
        DispatchActorLifecycle(_actorLeaveHandler, info);
    }

    private static unsafe void DispatchActorLifecycle(
        Action<SpotActorLifecycleInfo>? handler,
        ZlinkSpotActorLifecycleInfo* info)
    {
        if (handler == null || info == null)
            return;

        SpotActorLifecycleInfo managed = ActorInterop.FromNative(ref *info);
        try
        {
            handler(managed);
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
        }
    }

}
