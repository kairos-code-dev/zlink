// SPDX-License-Identifier: MPL-2.0

using System;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed partial class Spot
{
    private ReceivedReplyHandler CreateRoutedReplyHandler(
        RoutingId? nodeRid,
        RoutingId? spotRid,
        ulong requestSeq)
    {
        RoutingId targetNode = nodeRid ?? throw new ZlinkSubmitException(
            SubmitResult.InvalidArgument, (int)ErrorCode.EInval);
        return spotRid.HasValue
            ? (replyParts, sendFlags) => ReplyToSpot(
                targetNode,
                spotRid.Value,
                requestSeq,
                replyParts,
                sendFlags)
            : (replyParts, sendFlags) => ReplyToRouter(
                targetNode,
                requestSeq,
                replyParts,
                sendFlags);
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

    private ReceivedSendSingleHandler? CreateRoutedSendSingleHandler(
        RoutingId? nodeRid, RoutingId? spotRid)
    {
        if (!nodeRid.HasValue || !spotRid.HasValue)
            return null;
        RoutingId targetNode = nodeRid.Value;
        RoutingId targetSpot = spotRid.Value;
        return (sendPart, sendFlags) => SendToSpot(targetNode, targetSpot,
            sendPart, sendFlags);
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
            SpotDispatchHandler? dispatchHandler = _dispatchEventHandler;
            if (dispatchHandler == null)
                return;

            try
            {
                dispatchHandler(SpotDispatchInfo.SubscribeReadableSpot);
            }
            catch (Exception ex)
            {
                CallbackExceptionHub.Report(ex);
            }
            return;
        }

        SpotDispatchHandler? handler = _dispatchEventHandler;
        if (handler == null)
            return;

        ActorReceived[]? actorMessages = eventKind == SpotDispatchEvent.ActorReadable
            && subjectKind == SpotDispatchSubjectKind.Actor
            && info->Subject != IntPtr.Zero
                ? ActorInterop.DrainActors(_node.Handle, info->Subject)
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
        if (eventKind == SpotDispatchEvent.ChannelReplyReadable
            && subjectKind == SpotDispatchSubjectKind.Spot)
        {
            try
            {
                DrainReplies();
            }
            catch (Exception ex)
            {
                CallbackExceptionHub.Report(ex);
            }
        }

        SpotDispatchInfo dispatchInfo = new(eventKind, subjectKind,
            timer, channelDealerSubject, DrainChannelReplyFrom, actorMessages);
        try
        {
            handler(dispatchInfo);
        }
        catch (Exception ex)
        {
            CallbackExceptionHub.Report(ex);
        }
    }

}
