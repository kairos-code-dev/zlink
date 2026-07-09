// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

internal sealed partial class Spot : ISpot
{
    public SendOperation Publish(string topic)
    {
        return new SpotSendOperation(this, SpotOperationKind.Publish,
            topic: topic);
    }

    public SendOperation SendToChannel(string channelName)
    {
        return new SpotSendOperation(this, SpotOperationKind.SendToChannel,
            channelName: channelName);
    }

    public RequestOperation RequestToChannel(string channelName)
    {
        return new SpotRequestOperation(this, SpotOperationKind.RequestToChannel,
            channelName);
    }

    public SendOperation SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid)
    {
        return new SpotSendOperation(this, SpotOperationKind.SendToSpot,
            destNodeRid: destNodeRid, destSpotRid: destSpotRid);
    }

    public RequestOperation RequestToSpot(RoutingId destNodeRid,
        RoutingId destSpotRid)
    {
        return new SpotRequestOperation(this, SpotOperationKind.RequestToSpot,
            destNodeRid: destNodeRid, destSpotRid: destSpotRid);
    }

    public RequestOperation RequestToRouter(RoutingId peerRid)
    {
        return new SpotRequestOperation(this, SpotOperationKind.RequestToRouter,
            peerRid: peerRid);
    }

    public ReplyOperation ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSeq)
    {
        return new SpotReplyOperation(this, SpotOperationKind.ReplyToSpot,
            destNodeRid, destSpotRid,
            requestSeq: requestSeq);
    }

    public ReplyOperation ReplyToRouter(RoutingId peerRid, ulong requestSeq)
    {
        return new SpotReplyOperation(this, SpotOperationKind.ReplyToRouter,
            peerRid: peerRid, requestSeq: requestSeq);
    }
}
