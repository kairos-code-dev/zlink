// 자립형 가이드 예제: SPOT Actor의 재접속 이전성(single-player queue).
// 한 파일 안에 전체 흐름이 들어 있다 — 별도 헬퍼 없이 빌드·실행된다:
//   cargo run --example actor_queue_example

use std::sync::mpsc;
use std::sync::{Arc, Mutex};
use std::thread::sleep;
use std::time::{Duration, Instant};

use zlink::{
    ActorReceived, Context, Message, RecvFlags, SendFlags, Spot, SpotDispatchEvent, SpotNode,
};

// join 요청을 수신해 "accepted"로 응답한다.
fn accept(spot: &Spot) {
    let deadline = Instant::now() + Duration::from_secs(2);
    loop {
        if let Ok(Some(request)) = spot.recv_actor_join_with_flags(RecvFlags::DONT_WAIT) {
            spot.reply_actor_join(&request, 0)
                .message(Message::try_from(b"accepted").unwrap())
                .submit()
                .unwrap();
            return;
        }
        assert!(Instant::now() < deadline, "join request not received");
        sleep(Duration::from_millis(10));
    }
}

fn main() {
    let ctx = Context::new().unwrap();
    let node = SpotNode::new(&ctx).unwrap();
    let mut spot = node.create_spot().unwrap();
    let mut actor = node.create_actor("single-player").unwrap();
    let stream = ctx.stream_socket().unwrap();

    // 스트림 게이트웨이에 actor를 세션으로 바인딩한다. 실제 서버에서 session은
    // 게이트웨이로 접속한 클라이언트의 라우팅 ID다 — 여기선 고정값으로 만든다.
    stream.attach_actor_gateway(&node).unwrap();
    let session = zlink::RoutingId::from(b"single-player-session");
    let (bind_tx, bind_rx) = mpsc::channel();
    stream
        .bind_actor(&session, &actor.actor_ref().unwrap())
        .timeout(Duration::from_secs(1))
        .submit(move |result| bind_tx.send(result).unwrap())
        .unwrap();
    bind_rx
        .recv_timeout(Duration::from_secs(2))
        .unwrap()
        .unwrap();

    // dispatch 핸들러: actor에게 온 메시지를 모은다.
    let payloads = Arc::new(Mutex::new(Vec::<String>::new()));
    let sink = Arc::clone(&payloads);
    spot.on_dispatch_event(move |info| {
        if info.event != SpotDispatchEvent::ActorReadable {
            return;
        }
        let mut received = ActorReceived::empty();
        while let Ok(true) = info.recv_actor(&mut received, RecvFlags::DONT_WAIT) {
            sink.lock()
                .unwrap()
                .push(received.first_part().unwrap().as_str().unwrap().to_owned());
        }
    })
    .unwrap();

    // join-first → "before"는 joined 상태에서 도착
    let (tx1, rx1) = mpsc::channel();
    actor
        .join(&spot)
        .message(Message::try_from(b"join-first").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .timeout(Duration::from_secs(1))
        .submit(move |result, _| tx1.send(result).unwrap())
        .unwrap();
    accept(&spot);
    assert_eq!(
        rx1.recv_timeout(Duration::from_secs(2)).unwrap().result,
        zlink::RequestResult::Ok
    );
    stream
        .send_bound_actor(&session, "single-player")
        .message(Message::try_from(b"before").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .submit()
        .unwrap();

    // leave → "between"은 leave 사이에 도착 → 큐잉
    let (lt, lr) = mpsc::channel();
    actor
        .leave(&spot)
        .timeout(Duration::from_secs(1))
        .submit(move |r| lt.send(r).unwrap())
        .unwrap();
    lr.recv_timeout(Duration::from_secs(2)).unwrap().unwrap();
    stream
        .send_bound_actor(&session, "single-player")
        .message(Message::try_from(b"between").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .submit()
        .unwrap();

    // rejoin → 큐된 "before"/"between"이 핸들러로 배달된다
    let (tx2, rx2) = mpsc::channel();
    actor
        .join(&spot)
        .message(Message::try_from(b"join-second").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .timeout(Duration::from_secs(1))
        .submit(move |result, _| tx2.send(result).unwrap())
        .unwrap();
    accept(&spot);
    assert_eq!(
        rx2.recv_timeout(Duration::from_secs(2)).unwrap().result,
        zlink::RequestResult::Ok
    );

    let deadline = Instant::now() + Duration::from_secs(2);
    while payloads.lock().unwrap().len() < 2 && Instant::now() < deadline {
        sleep(Duration::from_millis(10));
    }
    assert_eq!(*payloads.lock().unwrap(), ["before", "between"]);

    let (lt2, lr2) = mpsc::channel();
    actor
        .leave(&spot)
        .timeout(Duration::from_secs(1))
        .submit(move |r| lt2.send(r).unwrap())
        .unwrap();
    lr2.recv_timeout(Duration::from_secs(2)).unwrap().unwrap();
    actor.close().unwrap();
    println!(
        "[actor/single-player] queued payload: \"before/between\" -> actor: \"before/between\""
    );
}
