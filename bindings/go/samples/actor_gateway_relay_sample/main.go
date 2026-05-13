package main

import (
	"fmt"
	"time"
	"zlink.systems/zlink"
	"zlink.systems/zlink/samples/internal/samplecommon"
)

func main() {
	ctx, err := zlink.NewContext()
	samplecommon.Must(err)
	defer ctx.Close()

	gatewayNode, err := ctx.SpotNode()
	samplecommon.Must(err)
	defer gatewayNode.Close()
	playSpot, err := gatewayNode.Spot()
	samplecommon.Must(err)
	defer playSpot.Close()

	actor, err := gatewayNode.Actor("play-session-actor")
	samplecommon.Must(err)

	actorRef := actor.Ref()

	joinCh := make(chan zlink.RequestResult, 1)
	playSpotRID := mustRID(playSpot.RoutingID())
	playNodeRID := mustRID(gatewayNode.RoutingID())
	_, joinErr := gatewayNode.JoinActor(actorRef, playNodeRID, playSpotRID).Message(samplecommon.Message("join-play")).Flags(zlink.SendFlagsDontWait).Timeout(time.Second).Submit(nil, func(result zlink.ActorJoinResult, parts []*zlink.Message) {
		for _, part := range parts {
			part.Close()
		}
		joinCh <- result.Result
	})
	samplecommon.Must(joinErr)
	request, err := playSpot.RecvActorJoin(zlink.RecvFlagsDontWait)
	samplecommon.Must(err)
	if string(request.Message.Data()) != "join-play" {
		samplecommon.Must(fmt.Errorf("unexpected join payload"))
	}
	request.Message.Close()
	replyErr := playSpot.ReplyActorJoin(request, true).Message(samplecommon.Message("accepted")).Submit(nil)
	samplecommon.Must(replyErr)
	if result := <-joinCh; result != zlink.RequestOK {
		samplecommon.Must(fmt.Errorf("unexpected join result %v", result))
	}

	leaveCh, err := gatewayNode.LeaveActor(actorRef, playSpotRID).Timeout(time.Second).SubmitAsync(nil)
	samplecommon.Must(err)
	leave := <-leaveCh
	samplecommon.Must(leave.Err)
	leaveParts := leave.Parts
	zlink.MultipartClose(leaveParts)
	destroyCh, err := gatewayNode.DestroyRemoteActor(actorRef).Timeout(time.Second).SubmitAsync(nil)
	samplecommon.Must(err)
	destroy := <-destroyCh
	samplecommon.Must(destroy.Err)
	destroyParts := destroy.Parts
	zlink.MultipartClose(destroyParts)
}

func mustRID(rid zlink.RoutingID, err error) zlink.RoutingID {
	samplecommon.Must(err)
	return rid
}
