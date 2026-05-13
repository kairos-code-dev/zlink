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

	node, err := ctx.SpotNode()
	samplecommon.Must(err)
	defer node.Close()
	firstSpot, err := node.Spot()
	samplecommon.Must(err)
	defer firstSpot.Close()
	secondSpot, err := node.Spot()
	samplecommon.Must(err)
	defer secondSpot.Close()
	actor, err := node.Actor("single-player")
	samplecommon.Must(err)

	ref := actor.Ref()

	firstJoin := make(chan zlink.RequestResult, 1)
	_, joinErr := actor.Join(firstSpot).Message(samplecommon.Message("join-first")).Flags(zlink.SendFlagsDontWait).Timeout(time.Second).Submit(nil, func(result zlink.ActorJoinResult, parts []*zlink.Message) {
		for _, part := range parts {
			part.Close()
		}
		firstJoin <- result.Result
	})
	samplecommon.Must(joinErr)
	acceptJoin(firstSpot, "join-first")
	if result := <-firstJoin; result != zlink.RequestOK {
		samplecommon.Must(fmt.Errorf("unexpected first join result %v", result))
	}

	leaveCh, err := actor.Leave(firstSpot).Timeout(time.Second).SubmitAsync(nil)
	samplecommon.Must(err)
	leave := <-leaveCh
	samplecommon.Must(leave.Err)
	leaveParts := leave.Parts
	zlink.MultipartClose(leaveParts)

	secondJoin := make(chan zlink.RequestResult, 1)
	_, joinErr2 := node.JoinActor(ref, mustRID(node.RoutingID()), mustRID(secondSpot.RoutingID())).Message(samplecommon.Message("join-second")).Flags(zlink.SendFlagsDontWait).Timeout(time.Second).Submit(nil, func(result zlink.ActorJoinResult, parts []*zlink.Message) {
		for _, part := range parts {
			part.Close()
		}
		secondJoin <- result.Result
	})
	samplecommon.Must(joinErr2)
	acceptJoin(secondSpot, "join-second")
	if result := <-secondJoin; result != zlink.RequestOK {
		samplecommon.Must(fmt.Errorf("unexpected second join result %v", result))
	}
	leaveCh2, err := actor.Leave(secondSpot).Timeout(time.Second).SubmitAsync(nil)
	samplecommon.Must(err)
	leave2 := <-leaveCh2
	samplecommon.Must(leave2.Err)
	leaveParts2 := leave2.Parts
	zlink.MultipartClose(leaveParts2)
	samplecommon.Must(actor.Close())
}

func acceptJoin(spot *zlink.Spot, expected string) {
	request, err := spot.RecvActorJoin(zlink.RecvFlagsDontWait)
	samplecommon.Must(err)
	if string(request.Message.Data()) != expected {
		samplecommon.Must(fmt.Errorf("unexpected join payload %q", string(request.Message.Data())))
	}
	request.Message.Close()
	replyErr := spot.ReplyActorJoin(request, true).Message(samplecommon.Message("accepted")).Submit(nil)
	samplecommon.Must(replyErr)
}

func mustRID(rid zlink.RoutingID, err error) zlink.RoutingID {
	samplecommon.Must(err)
	return rid
}
