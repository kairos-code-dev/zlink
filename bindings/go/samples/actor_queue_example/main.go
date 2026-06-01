// 자립형 가이드 예제: SPOT Actor의 재접속 이전성(single-player queue).
// 한 파일 안에 전체 흐름이 들어 있다 — 별도 헬퍼 없이 그대로 빌드·실행된다:
//   go run ./samples/actor_queue_example
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
	spot, err := node.Spot()
	must(err)
	defer spot.Close()
	actor, err := node.Actor("single-player")
	must(err)
	stream, err := ctx.StreamSocket()
	must(err)
	defer stream.Close()

	// 스트림 게이트웨이에 actor를 세션으로 바인딩한다. 실제 서버에서 session은
	// 게이트웨이로 접속한 클라이언트의 라우팅 ID다 — 여기선 고정값으로 만든다.
	must(stream.AttachActorGateway(node))
	session := zlink.NewRoutingIDString("single-player-session")
	bindCh, err := stream.BindActor(session, actor.Ref()).Timeout(time.Second).SubmitAsync(nil)
	must(err)
	zlink.MultipartClose((<-bindCh).Parts)

	// dispatch 핸들러: join 요청을 수락하고, actor에게 온 메시지를 모은다.
	var mu sync.Mutex
	var payloads []string
	must(spot.OnDispatchEvent(func(current *zlink.Spot, info zlink.SpotDispatchInfo) {
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
				payloads = append(payloads, string(part.Message.Data()))
				mu.Unlock()
				part.Message.Close()
			}
		}
	}))

	join := func(payload string) {
		done := make(chan zlink.RequestResult, 1)
		_, joinErr := actor.Join(spot).Message(message(payload)).Timeout(time.Second).
			Submit(nil, func(result zlink.ActorJoinResult, parts []*zlink.Message) {
				zlink.MultipartClose(parts)
				done <- result.Result
			})
		must(joinErr)
		if result := <-done; result != zlink.RequestOK {
			panic(fmt.Sprintf("join %q failed: %v", payload, result))
		}
	}
	send := func(payload string) {
		_, sendErr := stream.SendBoundActor(session, "single-player").Message(message(payload)).Submit(nil)
		must(sendErr)
	}
	leave := func() {
		leaveCh, leaveErr := actor.Leave(spot).Timeout(time.Second).SubmitAsync(nil)
		must(leaveErr)
		zlink.MultipartClose((<-leaveCh).Parts)
	}

	join("join-first")  // actor가 spot에 합류
	send("before")      // joined 상태에서 도착
	leave()             // 처리 위치 이탈 (세션 바인딩은 유지)
	send("between")     // leave 사이에 도착 → 큐잉
	join("join-second") // rejoin → 큐된 메시지가 핸들러로 배달된다

	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		mu.Lock()
		n := len(payloads)
		mu.Unlock()
		if n >= 2 {
			break
		}
		time.Sleep(10 * time.Millisecond)
	}
	mu.Lock()
	got := append([]string(nil), payloads...)
	mu.Unlock()
	if len(got) != 2 || got[0] != "before" || got[1] != "between" {
		panic(fmt.Sprintf("queued payloads were not preserved: %v", got))
	}

	leave()
	must(actor.Close())
	fmt.Println(`[actor/single-player] queued payload: "before/between" -> actor: "before/between"`)
}
