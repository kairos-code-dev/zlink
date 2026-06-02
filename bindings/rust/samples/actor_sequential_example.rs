// 자립형 가이드 예제: STREAM이 relay한 메시지를 Actor가 순서대로 처리.
// Actor는 생성 시 Entry Spot(로비)에 있다가 join으로 개별 room(user Spot)으로
// 옮겨 간다. 메시지는 STREAM session에 actor를 bind하고 packet을 relay해야만
// 도달하며, room의 dispatch context에서 들어온 순서대로 처리된다.
//   cargo run --example actor_sequential_example

use std::sync::mpsc;
use std::sync::{Arc, Mutex};
use std::thread::sleep;
use std::time::{Duration, Instant};

use zlink::{
    ActorReceived, Context, Message, RecvFlags, RequestResult, RoutingId, SendFlags, Spot,
    SpotDispatchEvent, SpotNode,
};

// join 요청을 수신해 "accepted"로 응답한다.
fn accept(room: &Spot) {
    let deadline = Instant::now() + Duration::from_secs(2);
    loop {
        if let Ok(Some(request)) = room.recv_actor_join_with_flags(RecvFlags::DONT_WAIT) {
            room.reply_actor_join(&request, 0)
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
// --8<-- [start:doc]
    let ctx = Context::new().unwrap();
    let node = SpotNode::new(&ctx).unwrap();
    let mut room = node.create_spot().unwrap();
    // 생성 직후 actor는 Entry Spot(로비)에 위치한다.
    let mut player = node.create_actor("player").unwrap();
    let stream = ctx.stream_socket().unwrap();
    stream.attach_actor_gateway(&node).unwrap();
    let session = RoutingId::from(b"player-session");

    // STREAM session에 actor를 bind한다 (이후 relay가 이 actor로 간다).
    let (bind_tx, bind_rx) = mpsc::channel();
    stream
        .bind_actor(&session, &player.actor_ref().unwrap())
        .timeout(Duration::from_secs(1))
        .submit(move |r| bind_tx.send(r).unwrap())
        .unwrap();
    bind_rx.recv_timeout(Duration::from_secs(2)).unwrap().unwrap();

    // dispatch 핸들러: STREAM이 relay한 메시지를 모은다.
    let processed = Arc::new(Mutex::new(Vec::<String>::new()));
    let sink = Arc::clone(&processed);
    room.on_dispatch_event(move |info| {
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

    // join으로 Entry Spot에서 room(user Spot)으로 이동한다.
    let (join_tx, join_rx) = mpsc::channel();
    player
        .join(&room)
        .message(Message::try_from(b"enter-room").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .timeout(Duration::from_secs(1))
        .submit(move |result, _| join_tx.send(result).unwrap())
        .unwrap();
    accept(&room);
    assert_eq!(
        join_rx.recv_timeout(Duration::from_secs(2)).unwrap().result,
        RequestResult::Ok
    );

    // STREAM이 플레이어 입력을 연달아 relay한다 — actor는 순서대로 처리한다.
    for command in ["move", "attack", "loot"] {
        stream
            .send_bound_actor(&session, "player")
            .message(Message::try_from(command.as_bytes()).unwrap())
            .flags(SendFlags::DONT_WAIT)
            .submit()
            .unwrap();
    }

    let deadline = Instant::now() + Duration::from_secs(2);
    while processed.lock().unwrap().len() < 3 && Instant::now() < deadline {
        sleep(Duration::from_millis(10));
    }
    assert_eq!(*processed.lock().unwrap(), ["move", "attack", "loot"]);

    let (leave_tx, leave_rx) = mpsc::channel();
    player
        .leave(&room)
        .timeout(Duration::from_secs(1))
        .submit(move |r| leave_tx.send(r).unwrap())
        .unwrap();
    leave_rx.recv_timeout(Duration::from_secs(2)).unwrap().unwrap();
    player.close().unwrap();
    println!("[actor/sequential] processed in order: move -> attack -> loot");
// --8<-- [end:doc]
}
