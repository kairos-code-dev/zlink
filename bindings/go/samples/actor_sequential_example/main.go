// 자립형 가이드 예제: STREAM이 relay한 메시지를 Actor가 순서대로 처리.
//
// Actor는 생성 시 Entry Spot(로비)에 있다가 join으로 개별 room(user Spot)으로
// 옮겨 간다. 메시지는 STREAM session에 actor를 bind하고 packet을 relay해야만
// 도달하며, room의 dispatch context에서 들어온 순서대로 처리된다.
//
//	go run ./samples/actor_sequential_example
package main

import (
	"fmt"
	"sync"
	"time"

	zlink "zlink.systems/zlink"
)

func must(err error) {
	if err != nil {
		panic(err)
	}
}

func message(text string) *zlink.Message {
	msg, err := zlink.NewMessageString(text)
	must(err)
	return msg
}

func main() {
	// --8<-- [start:doc]
	ctx, err := zlink.NewContext()
	must(err)
	defer ctx.Close()
	node, err := ctx.SpotNode()
	must(err)
	defer node.Close()
	room, err := node.Spot()
	must(err)
	defer room.Close()
	stream, err := ctx.StreamSocket()
	must(err)
	defer stream.Close()
	must(stream.AttachActorGateway(node))
	session := zlink.NewRoutingIDString("player-session")

	// 생성 직후 actor는 Entry Spot(로비)에 위치한다.
	player, err := node.Actor("player")
	must(err)

	// dispatch 핸들러: join을 수락하고, STREAM이 relay한 메시지를 모은다.
	var mu sync.Mutex
	var processed []string
	must(room.OnDispatchEvent(func(current *zlink.Spot, info zlink.SpotDispatchInfo) {
		switch info.Event {
		case zlink.SpotDispatchEventActorJoinReadable:
			request, recvErr := current.RecvActorJoin(zlink.RecvFlagsDontWait)
			if recvErr != nil || request == nil {
				return
			}
			request.Message.Close()
			_ = current.ReplyActorJoin(request, 0).Message(message("accepted")).Submit(nil)
		case zlink.SpotDispatchEventActorReadable:
			for {
				part, _ := info.RecvActorPart(zlink.RecvFlagsDontWait)
				if part == nil {
					return
				}
				mu.Lock()
				processed = append(processed, string(part.Message.Data()))
				mu.Unlock()
				part.Message.Close()
			}
		}
	}))

	// STREAM session에 actor를 bind한다 (이후 relay가 이 actor로 간다).
	bindCh, err := stream.BindActor(session, player.Ref()).Timeout(time.Second).SubmitAsync(nil)
	must(err)
	zlink.MultipartClose((<-bindCh).Parts)

	// join으로 Entry Spot에서 room(user Spot)으로 이동한다.
	joinDone := make(chan zlink.RequestResult, 1)
	_, err = player.Join(room).Message(message("enter-room")).Timeout(time.Second).
		Submit(nil, func(r zlink.ActorJoinResult, p []*zlink.Message) {
			zlink.MultipartClose(p)
			joinDone <- r.Result
		})
	must(err)
	<-joinDone

	// STREAM이 플레이어 입력을 연달아 relay한다 — actor는 들어온 순서대로 처리한다.
	commands := []string{"move", "attack", "loot"}
	for _, command := range commands {
		_, e := stream.SendBoundActor(session, "player").Message(message(command)).Submit(nil)
		must(e)
	}

	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		mu.Lock()
		n := len(processed)
		mu.Unlock()
		if n >= len(commands) {
			break
		}
		time.Sleep(10 * time.Millisecond)
	}
	mu.Lock()
	got := append([]string(nil), processed...)
	mu.Unlock()
	if len(got) != 3 || got[0] != "move" || got[1] != "attack" || got[2] != "loot" {
		panic(fmt.Sprintf("messages were not processed in order: %v", got))
	}

	leaveCh, err := player.Leave(room).Timeout(time.Second).SubmitAsync(nil)
	must(err)
	zlink.MultipartClose((<-leaveCh).Parts)
	must(player.Close())
	fmt.Println(`[actor/sequential] processed in order: move -> attack -> loot`)
	// --8<-- [end:doc]
}
