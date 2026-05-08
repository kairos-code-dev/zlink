package main

import (
	"bytes"
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
	ref := actor.Ref()
	samplecommon.Must(stream.BindActor(node, session, ref, time.Second))

	joinCh := make(chan zlink.RequestResult, 1)
	_, joinErr := actor.Join(spot, samplecommon.Message("enter-room"), func(result zlink.RequestResult, parts []*zlink.Message) {
		for _, part := range parts {
			part.Close()
		}
		joinCh <- result
	}, zlink.SendFlagsDontWait, time.Second)
	samplecommon.Must(joinErr)
	request, err := spot.RecvActorJoin(zlink.RecvFlagsDontWait)
	samplecommon.Must(err)
	if !bytes.Equal(request.Message.Data(), []byte("enter-room")) {
		samplecommon.Must(fmt.Errorf("unexpected join payload"))
	}
	request.Message.Close()
	_, replyErr := spot.ReplyActorJoin(request, zlink.ActorAdmissionAccept, zlink.SendFlagsNone, samplecommon.Message("accepted"))
	samplecommon.Must(replyErr)
	if result := <-joinCh; result != zlink.RequestOK {
		samplecommon.Must(fmt.Errorf("unexpected join result %v", result))
	}

	_, sendErr := stream.SendBoundActor(node, session, samplecommon.Message("move:north"), zlink.SendFlagsDontWait)
	samplecommon.Must(sendErr)

	select {
	case payload := <-payloadCh:
		if payload != "move:north" {
			samplecommon.Must(fmt.Errorf("unexpected actor payload %q", payload))
		}
	case <-time.After(2 * time.Second):
		samplecommon.Must(fmt.Errorf("timed out waiting for actor payload"))
	}

	samplecommon.Must(actor.Leave(spot, time.Second))
	samplecommon.Must(actor.Close())
}
