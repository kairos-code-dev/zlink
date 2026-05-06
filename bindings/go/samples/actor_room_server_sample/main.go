package main

import (
	"bytes"
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
	spot, err := node.Spot()
	samplecommon.Must(err)
	defer spot.Close()
	actor, err := node.Actor("room-player-1")
	samplecommon.Must(err)

	payloadCh := make(chan string, 1)
	samplecommon.Must(spot.OnDispatchEvent(func(_ *zlink.Spot, info zlink.SpotDispatchInfo) {
		if info.Event != zlink.SpotDispatchEventActorReadable {
			return
		}
		part, err := info.RecvActorPart(zlink.RecvFlagsDontWait)
		if err == nil && part != nil {
			payloadCh <- string(part.Message.Data())
			part.Message.Close()
		}
	}))

	stream, err := ctx.StreamSocket()
	samplecommon.Must(err)
	defer stream.Close()
	session := zlink.NewRoutingID([]byte("room-session"))
	ref, err := actor.Ref()
	samplecommon.Must(err)
	samplecommon.Must(stream.BindActor(node, session, ref, time.Second))

	joinCh := make(chan zlink.RequestResult, 1)
	samplecommon.Must(actor.Join(spot, func(result zlink.RequestResult, parts []*zlink.Message) {
		for _, part := range parts {
			part.Close()
		}
		joinCh <- result
	}, zlink.SendFlagsDontWait, time.Second, samplecommon.Message("enter-room")))
	info, message, err := spot.RecvActorJoin(zlink.RecvFlagsDontWait)
	samplecommon.Must(err)
	if !bytes.Equal(message.Data(), []byte("enter-room")) {
		samplecommon.Must(fmt.Errorf("unexpected join payload"))
	}
	message.Close()
	samplecommon.Must(spot.ReplyActorJoin(info, true, samplecommon.Message("accepted")))
	if result := <-joinCh; result != zlink.RequestOK {
		samplecommon.Must(fmt.Errorf("unexpected join result %v", result))
	}

	samplecommon.Must(stream.SendBoundActor(node, session, "room-player-1", zlink.SendFlagsDontWait, samplecommon.Message("move:north")))

	select {
	case payload := <-payloadCh:
		if payload != "move:north" {
			samplecommon.Must(fmt.Errorf("unexpected actor payload %q", payload))
		}
	case <-time.After(2 * time.Second):
		samplecommon.Must(fmt.Errorf("timed out waiting for actor payload"))
	}

	samplecommon.Must(actor.Leave(spot))
	samplecommon.Must(actor.Close())
}
