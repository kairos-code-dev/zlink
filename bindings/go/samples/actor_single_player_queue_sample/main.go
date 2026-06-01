package main

import (
	"fmt"
	"sync"
	"time"
	zlink "zlink.systems/zlink/contracts"
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
	actor, err := node.Actor("single-player")
	samplecommon.Must(err)
	ref := actor.Ref()

	stream, err := ctx.StreamSocket()
	samplecommon.Must(err)
	defer stream.Close()
	samplecommon.Must(stream.AttachActorGateway(node))
	session := zlink.NewRoutingIDString("single-player-session")

	var mu sync.Mutex
	var payloads []string
	// The dispatch handler accepts join requests and drains actor messages.
	samplecommon.Must(spot.OnDispatchEvent(func(current *zlink.Spot, info zlink.SpotDispatchInfo) {
		switch info.Event {
		case zlink.SpotDispatchEventActorJoinReadable:
			request, recvErr := current.RecvActorJoin(zlink.RecvFlagsDontWait)
			if recvErr != nil || request == nil {
				return
			}
			request.Message.Close()
			_ = current.ReplyActorJoin(request, 0).Message(samplecommon.Message("accepted")).Submit(nil)
		case zlink.SpotDispatchEventActorReadable:
			for {
				part, _ := info.RecvActorPart(zlink.RecvFlagsDontWait)
				if part == nil {
					return
				}
				mu.Lock()
				payloads = append(payloads, string(part.Message.Data()))
				mu.Unlock()
				part.Message.Close()
			}
		}
	}))

	// Bind the actor to a session on the stream gateway so session-bound
	// relay sends reach it.
	bindCh, err := stream.BindActor(session, ref).Timeout(time.Second).SubmitAsync(nil)
	samplecommon.Must(err)
	bind := <-bindCh
	samplecommon.Must(bind.Err)
	zlink.MultipartClose(bind.Parts)

	// First join. The dispatch handler accepts it.
	firstJoin := make(chan zlink.RequestResult, 1)
	_, joinErr := actor.Join(spot).Message(samplecommon.Message("join-first")).Flags(zlink.SendFlagsDontWait).Timeout(time.Second).Submit(nil, func(result zlink.ActorJoinResult, parts []*zlink.Message) {
		zlink.MultipartClose(parts)
		firstJoin <- result.Result
	})
	samplecommon.Must(joinErr)
	if result := <-firstJoin; result != zlink.RequestOK {
		samplecommon.Must(fmt.Errorf("unexpected first join result %v", result))
	}

	// "before" arrives while the actor is joined.
	_, err = stream.SendBoundActor(session, "single-player").Message(samplecommon.Message("before")).Submit(nil)
	samplecommon.Must(err)

	// Leave: the session stays bound, so messages that arrive now are queued.
	leaveCh, err := actor.Leave(spot).Timeout(time.Second).SubmitAsync(nil)
	samplecommon.Must(err)
	leave := <-leaveCh
	samplecommon.Must(leave.Err)
	zlink.MultipartClose(leave.Parts)

	// "between" arrives during the leave gap and is preserved.
	_, err = stream.SendBoundActor(session, "single-player").Message(samplecommon.Message("between")).Submit(nil)
	samplecommon.Must(err)

	// Rejoin: the queued messages are delivered to the dispatch handler.
	secondJoin := make(chan zlink.RequestResult, 1)
	_, joinErr2 := actor.Join(spot).Message(samplecommon.Message("join-second")).Flags(zlink.SendFlagsDontWait).Timeout(time.Second).Submit(nil, func(result zlink.ActorJoinResult, parts []*zlink.Message) {
		zlink.MultipartClose(parts)
		secondJoin <- result.Result
	})
	samplecommon.Must(joinErr2)
	if result := <-secondJoin; result != zlink.RequestOK {
		samplecommon.Must(fmt.Errorf("unexpected second join result %v", result))
	}

	samplecommon.WaitUntil(2*time.Second, "queued actor payloads", func() bool {
		mu.Lock()
		defer mu.Unlock()
		return len(payloads) >= 2
	})
	mu.Lock()
	got := append([]string(nil), payloads...)
	mu.Unlock()
	if len(got) != 2 || got[0] != "before" || got[1] != "between" {
		samplecommon.Must(fmt.Errorf("queued payloads were not preserved: %v", got))
	}

	leaveCh2, err := actor.Leave(spot).Timeout(time.Second).SubmitAsync(nil)
	samplecommon.Must(err)
	leave2 := <-leaveCh2
	samplecommon.Must(leave2.Err)
	zlink.MultipartClose(leave2.Parts)
	_, err = stream.UnbindActor(session, "single-player").Timeout(time.Second).SubmitAsync(nil)
	samplecommon.Must(err)
	samplecommon.Must(actor.Close())
	fmt.Println("[actor/single-player] queued payload: \"before/between\" -> actor: \"before/between\"")
}
