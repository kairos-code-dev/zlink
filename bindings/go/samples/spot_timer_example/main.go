// 자립형 가이드 예제: SPOT timer.
// 게임룸(Spot)이 주기 타이머로 틱을 돌린다(예: 게임 루프 틱/타임아웃).
//   go run ./samples/spot_timer_example
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

	// 게임룸의 이벤트 루프에서 디스패치되는 타이머를 만든다.
	timer, err := zlink.NewTimerFromSpot(room)
	must(err)
	defer timer.Close()

	var mu sync.Mutex
	ticks := 0
	must(timer.OnFire(func(t *zlink.Timer, fireCount uint64) {
		mu.Lock()
		ticks++
		mu.Unlock()
	}))
	// 50ms 간격으로 3번 발화한다.
	must(timer.Start(uint64(50*time.Millisecond), 3))

	deadline := time.Now().Add(3 * time.Second)
	for time.Now().Before(deadline) {
		mu.Lock()
		n := ticks
		mu.Unlock()
		if n >= 3 {
			break
		}
		time.Sleep(10 * time.Millisecond)
	}
	mu.Lock()
	final := ticks
	mu.Unlock()
	if final < 3 {
		panic(fmt.Sprintf("timer did not fire 3 times: %d", final))
	}
	fmt.Printf("[spot/timer] room tick fired %d times\n", final)
}
