// 자립형 가이드 예제: SPOT timer.
// 게임룸(Spot)이 주기 타이머로 틱을 돌린다(예: 게임 루프 틱/타임아웃).
//   cargo run --example spot_timer_example

use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::{Duration, Instant};

use zlink::{Context, SpotNode, Timer};

fn main() {
// --8<-- [start:doc]
    let ctx = Context::new().unwrap();
    let node = SpotNode::new(&ctx).unwrap();
    let room = node.create_spot().unwrap();

    // 게임룸의 이벤트 루프에서 디스패치되는 타이머를 만든다.
    let mut timer = Timer::from_spot(&room).unwrap();
    let ticks = Arc::new(AtomicUsize::new(0));
    let ticks_cb = Arc::clone(&ticks);
    timer
        .on_fire(move |_timer: &Timer, _fire_count: u64| {
            ticks_cb.fetch_add(1, Ordering::SeqCst);
        })
        .unwrap();
    // 50ms 간격으로 3번 발화한다.
    timer.start(50_000_000, 3).unwrap();

    let deadline = Instant::now() + Duration::from_secs(3);
    while ticks.load(Ordering::SeqCst) < 3 && Instant::now() < deadline {
        thread::sleep(Duration::from_millis(10));
    }
    let final_ticks = ticks.load(Ordering::SeqCst);
    assert!(final_ticks >= 3, "timer fired only {final_ticks} times");
    println!("[spot/timer] room tick fired {final_ticks} times");
// --8<-- [end:doc]
}
