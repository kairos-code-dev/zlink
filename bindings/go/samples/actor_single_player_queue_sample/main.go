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

	stream, err := ctx.StreamSocket()
	samplecommon.Must(err)
	defer stream.Close()
	session := zlink.NewRoutingID([]byte("single-player-session"))
	ref := actor.Ref()
	samplecommon.Must(stream.BindActor(node, session, ref, time.Second))

	firstJoin := make(chan zlink.RequestResult, 1)
	_, joinErr := actor.Join(firstSpot, samplecommon.Message("join-first"), func(result zlink.RequestResult, parts []*zlink.Message) {
		for _, part := range parts {
			part.Close()
		}
		firstJoin <- result
	}, zlink.SendFlagsDontWait, time.Second)
	samplecommon.Must(joinErr)
	acceptJoin(firstSpot, "join-first")
	if result := <-firstJoin; result != zlink.RequestOK {
		samplecommon.Must(fmt.Errorf("unexpected first join result %v", result))
	}

	_, sendErr1 := stream.SendBoundActor(node, session, samplecommon.Message("before"), zlink.SendFlagsDontWait)
	samplecommon.Must(sendErr1)
	samplecommon.Must(actor.Leave(firstSpot, time.Second))
	_, sendErr2 := stream.SendBoundActor(node, session, samplecommon.Message("between"), zlink.SendFlagsDontWait)
	samplecommon.Must(sendErr2)

	payloads := make(chan string, 2)
	samplecommon.Must(secondSpot.OnDispatchEvent(func(_ *zlink.Spot, info zlink.SpotDispatchInfo) {
		if info.Event != zlink.SpotDispatchEventActorReadable {
			return
		}
		for {
			part, err := info.RecvActorPart(zlink.RecvFlagsDontWait)
			if err != nil || part == nil {
				return
			}
			payloads <- string(part.Message.Data())
			part.Message.Close()
		}
	}))
	secondJoin := make(chan zlink.RequestResult, 1)
	_, joinErr2 := node.JoinActor(ref, mustRID(node.RoutingID()), mustRID(secondSpot.RoutingID()), samplecommon.Message("join-second"), func(result zlink.RequestResult, parts []*zlink.Message) {
		for _, part := range parts {
			part.Close()
		}
		secondJoin <- result
	}, zlink.SendFlagsDontWait, time.Second)
	samplecommon.Must(joinErr2)
	acceptJoin(secondSpot, "join-second")
	if result := <-secondJoin; result != zlink.RequestOK {
		samplecommon.Must(fmt.Errorf("unexpected second join result %v", result))
	}
	first := waitPayload(payloads)
	second := waitPayload(payloads)
	if first != "before" || second != "between" {
		samplecommon.Must(fmt.Errorf("unexpected payload order %q %q", first, second))
	}
	samplecommon.Must(actor.Leave(secondSpot, time.Second))
	samplecommon.Must(actor.Close())
}

func acceptJoin(spot *zlink.Spot, expected string) {
	request, err := spot.RecvActorJoin(zlink.RecvFlagsDontWait)
	samplecommon.Must(err)
	if string(request.Message.Data()) != expected {
		samplecommon.Must(fmt.Errorf("unexpected join payload %q", string(request.Message.Data())))
	}
	request.Message.Close()
	_, replyErr := spot.ReplyActorJoin(request, zlink.ActorAdmissionAccept, zlink.SendFlagsNone, samplecommon.Message("accepted"))
	samplecommon.Must(replyErr)
}

func waitPayload(ch <-chan string) string {
	select {
	case payload := <-ch:
		return payload
	case <-time.After(2 * time.Second):
		samplecommon.Must(fmt.Errorf("timed out waiting for actor payload"))
		return ""
	}
}

func mustRID(rid zlink.RoutingID, err error) zlink.RoutingID {
	samplecommon.Must(err)
	return rid
}
