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

	joinCh := make(chan zlink.RequestResult, 1)
	_, joinErr := actor.Join(spot).Message(samplecommon.Message("enter-room")).Flags(zlink.SendFlagsDontWait).Timeout(time.Second).Submit(nil, func(result zlink.ActorJoinResult, parts []*zlink.Message) {
		for _, part := range parts {
			part.Close()
		}
		joinCh <- result.Result
	})
	samplecommon.Must(joinErr)
	request, err := spot.RecvActorJoin(zlink.RecvFlagsDontWait)
	samplecommon.Must(err)
	if !bytes.Equal(request.Message.Data(), []byte("enter-room")) {
		samplecommon.Must(fmt.Errorf("unexpected join payload"))
	}
	request.Message.Close()
	replyErr := spot.ReplyActorJoin(request, true).Message(samplecommon.Message("accepted")).Submit(nil)
	samplecommon.Must(replyErr)
	if result := <-joinCh; result != zlink.RequestOK {
		samplecommon.Must(fmt.Errorf("unexpected join result %v", result))
	}

	leaveCh, err := actor.Leave(spot).Timeout(time.Second).SubmitAsync(nil)
	samplecommon.Must(err)
	leave := <-leaveCh
	samplecommon.Must(leave.Err)
	leaveParts := leave.Parts
	zlink.MultipartClose(leaveParts)
	samplecommon.Must(actor.Close())
}
