package main

import (
	"fmt"
	"time"
	"zlink"
	"zlink/samples/internal/samplecommon"
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

	firstJoin := make(chan zlink.RequestResult, 1)
	samplecommon.Must(actor.Join(firstSpot, func(result zlink.RequestResult, parts []*zlink.Message) {
		for _, part := range parts {
			part.Close()
		}
		firstJoin <- result
	}, zlink.SendFlagsDontWait, time.Second, samplecommon.Message("join-first")))
	acceptJoin(firstSpot, "join-first")
	if result := <-firstJoin; result != zlink.RequestOK {
		samplecommon.Must(fmt.Errorf("unexpected first join result %v", result))
	}

	stream, err := ctx.StreamSocket()
	samplecommon.Must(err)
	defer stream.Close()
	session := zlink.NewRoutingID([]byte("single-player-session"))
	ref, err := actor.Ref()
	samplecommon.Must(err)
	samplecommon.Must(stream.BindActor(node, session, ref, time.Second))
	samplecommon.Must(stream.SendBoundActor(node, session, "single-player", zlink.SendFlagsDontWait, samplecommon.Message("before")))
	samplecommon.Must(actor.Leave(firstSpot))
	samplecommon.Must(stream.SendBoundActor(node, session, "single-player", zlink.SendFlagsDontWait, samplecommon.Message("between")))

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
	samplecommon.Must(node.JoinActor(ref, mustRID(secondSpot.RoutingID()), func(result zlink.RequestResult, parts []*zlink.Message) {
		for _, part := range parts {
			part.Close()
		}
		secondJoin <- result
	}, zlink.SendFlagsDontWait, time.Second, samplecommon.Message("join-second")))
	acceptJoin(secondSpot, "join-second")
	if result := <-secondJoin; result != zlink.RequestOK {
		samplecommon.Must(fmt.Errorf("unexpected second join result %v", result))
	}
	first := waitPayload(payloads)
	second := waitPayload(payloads)
	if first != "before" || second != "between" {
		samplecommon.Must(fmt.Errorf("unexpected payload order %q %q", first, second))
	}
	samplecommon.Must(actor.Leave(secondSpot))
	samplecommon.Must(actor.Close())
}

func acceptJoin(spot *zlink.Spot, expected string) {
	info, message, err := spot.RecvActorJoin(zlink.RecvFlagsDontWait)
	samplecommon.Must(err)
	if string(message.Data()) != expected {
		samplecommon.Must(fmt.Errorf("unexpected join payload %q", string(message.Data())))
	}
	message.Close()
	samplecommon.Must(spot.ReplyActorJoin(info, true, samplecommon.Message("accepted")))
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
