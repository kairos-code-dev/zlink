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
    public SendOperation Publish(string topic)
        => new SpotSendOperation(this, SpotOperationKind.Publish,
            topicOrChannel: topic);

    public SendOperation SendToChannel(string channelName)
        => new SpotSendOperation(this, SpotOperationKind.SendToChannel,
            topicOrChannel: channelName);

    public RequestOperation RequestToChannel(string channelName)
        => new SpotRequestOperation(this, SpotOperationKind.RequestToChannel,
            channelName: channelName);

    public SendOperation SendToSpot(RoutingId destNodeRid, RoutingId destSpotRid)
        => new SpotSendOperation(this, SpotOperationKind.SendToSpot,
            destNodeRid: destNodeRid, destSpotRid: destSpotRid);

    public RequestOperation RequestToSpot(RoutingId destNodeRid,
        RoutingId destSpotRid)
        => new SpotRequestOperation(this, SpotOperationKind.RequestToSpot,
            destNodeRid: destNodeRid, destSpotRid: destSpotRid);

    public RequestOperation RequestToRouter(RoutingId peerRid)
        => new SpotRequestOperation(this, SpotOperationKind.RequestToRouter,
            peerRid: peerRid);

    public ReplyOperation ReplyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
        ulong requestSeq)
        => new SpotReplyOperation(this, SpotOperationKind.ReplyToSpot,
            destNodeRid: destNodeRid, destSpotRid: destSpotRid,
            requestSeq: requestSeq);

    public ReplyOperation ReplyToRouter(RoutingId peerRid, ulong requestSeq)
        => new SpotReplyOperation(this, SpotOperationKind.ReplyToRouter,
            peerRid: peerRid, requestSeq: requestSeq);
}
