// 자립형 가이드 예제: 한 방(Spot)의 두 플레이어(Actor).
// 서버가 각 플레이어에게 id로 주소 지정해 메시지를 보내면, 그 Actor만 받는다.
// 한 파일로 빌드·실행된다:  go run ./samples/actor_room_example
package main

import (
	"fmt"
	"sync"
	"time"

	zlink "zlink.systems/zlink/contracts"
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
	session := zlink.NewRoutingIDString("game-room-session")

	// 한 방에 두 플레이어가 들어온다.
	player1, err := node.Actor("player-1")
	must(err)
	player2, err := node.Actor("player-2")
	must(err)

	// dispatch 핸들러: join 요청을 수락하고, 도착한 메시지를 모은다.
	var mu sync.Mutex
	var received []string
	must(room.OnDispatchEvent(func(current *zlink.Spot, info zlink.SpotDispatchInfo) {
		switch info.Event {
		case zlink.SpotDispatchEventActorJoinReadable:
			for {
				request, recvErr := current.RecvActorJoin(zlink.RecvFlagsDontWait)
				if recvErr != nil || request == nil {
					return
				}
				request.Message.Close()
				_ = current.ReplyActorJoin(request, 0).Message(message("accepted")).Submit(nil)
			}
		case zlink.SpotDispatchEventActorReadable:
			for {
				part, _ := info.RecvActorPart(zlink.RecvFlagsDontWait)
				if part == nil {
					return
				}
				mu.Lock()
				received = append(received, string(part.Message.Data()))
				mu.Unlock()
				part.Message.Close()
			}
		}
	}))

	bind := func(actor *zlink.Actor) {
		ch, e := stream.BindActor(session, actor.Ref()).Timeout(time.Second).SubmitAsync(nil)
		must(e)
		zlink.MultipartClose((<-ch).Parts)
	}
	join := func(actor *zlink.Actor) {
		done := make(chan zlink.RequestResult, 1)
		_, e := actor.Join(room).Message(message("enter-room")).Timeout(time.Second).
			Submit(nil, func(r zlink.ActorJoinResult, p []*zlink.Message) {
				zlink.MultipartClose(p)
				done <- r.Result
			})
		must(e)
		<-done
	}
	leave := func(actor *zlink.Actor) {
		ch, e := actor.Leave(room).Timeout(time.Second).SubmitAsync(nil)
		must(e)
		zlink.MultipartClose((<-ch).Parts)
	}
	// 메시지를 보내고, 도착해 핸들러에 쌓일 때까지 기다린다. 보낸 직후 받았으므로
	// 그 메시지는 방금 주소 지정한 플레이어의 것이다.
	sendAndWait := func(actorID, text string, want int) {
		_, e := stream.SendBoundActor(session, actorID).Message(message(text)).Submit(nil)
		must(e)
		deadline := time.Now().Add(2 * time.Second)
		for time.Now().Before(deadline) {
			mu.Lock()
			n := len(received)
			mu.Unlock()
			if n >= want {
				return
			}
			time.Sleep(10 * time.Millisecond)
		}
		panic(fmt.Sprintf("message to %s not delivered", actorID))
	}

	bind(player1)
	bind(player2)
	join(player1)
	join(player2)

	// 서버가 각 플레이어에게 자기 앞으로 온 메시지를 보낸다.
	sendAndWait("player-1", "your-turn", 1)
	sendAndWait("player-2", "wait", 2)

	if received[0] != "your-turn" || received[1] != "wait" {
		panic(fmt.Sprintf("messages were not routed per actor: %v", received))
	}

	leave(player1)
	leave(player2)
	must(player1.Close())
	must(player2.Close())
	fmt.Println(`[actor/room] player-1: "your-turn", player-2: "wait"`)
}
