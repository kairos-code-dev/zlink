// 자립형 가이드 예제: 한 방(Spot)의 두 플레이어(Actor).
// 서버가 각 플레이어에게 id로 주소 지정해 메시지를 보내면, 그 Actor만 받는다.
//   cargo run --example actor_room_example

use std::sync::mpsc;
use std::sync::{Arc, Mutex};
use std::thread::sleep;
use std::time::{Duration, Instant};

use zlink::{
// --8<-- [start:doc]
    Actor, ActorReceived, Context, Message, RecvFlags, RequestResult, RoutingId, SendFlags, Spot,
    SpotDispatchEvent, SpotNode, StreamSocket,
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

fn bind(stream: &StreamSocket, session: &RoutingId, actor: &Actor) {
    let (tx, rx) = mpsc::channel();
    stream
        .bind_actor(session, &actor.actor_ref().unwrap())
        .timeout(Duration::from_secs(1))
        .submit(move |r| tx.send(r).unwrap())
        .unwrap();
    rx.recv_timeout(Duration::from_secs(2)).unwrap().unwrap();
}

fn join(room: &Spot, actor: &Actor) {
    let (tx, rx) = mpsc::channel();
    actor
        .join(room)
        .message(Message::try_from(b"enter-room").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .timeout(Duration::from_secs(1))
        .submit(move |result, _| tx.send(result).unwrap())
        .unwrap();
    accept(room);
    assert_eq!(
        rx.recv_timeout(Duration::from_secs(2)).unwrap().result,
        RequestResult::Ok
    );
}

fn leave(room: &Spot, actor: &Actor) {
    let (tx, rx) = mpsc::channel();
    actor
        .leave(room)
        .timeout(Duration::from_secs(1))
        .submit(move |r| tx.send(r).unwrap())
        .unwrap();
    rx.recv_timeout(Duration::from_secs(2)).unwrap().unwrap();
}

// 보낸 직후 도착을 기다리므로, 그 메시지는 방금 주소 지정한 플레이어 것이다.
fn send_and_wait(
    stream: &StreamSocket, session: &RoutingId, actor_id: &str, text: &[u8],
    payloads: &Arc<Mutex<Vec<String>>>, want: usize,
) {
    stream
        .send_bound_actor(session, actor_id)
        .message(Message::try_from(text).unwrap())
        .flags(SendFlags::DONT_WAIT)
        .submit()
        .unwrap();
    let deadline = Instant::now() + Duration::from_secs(2);
    while payloads.lock().unwrap().len() < want && Instant::now() < deadline {
        sleep(Duration::from_millis(10));
    }
    assert!(payloads.lock().unwrap().len() >= want, "message to {actor_id} not delivered");
}

fn main() {
    let ctx = Context::new().unwrap();
    let node = SpotNode::new(&ctx).unwrap();
    let mut room = node.create_spot().unwrap();
    let mut player1 = node.create_actor("player-1").unwrap();
    let mut player2 = node.create_actor("player-2").unwrap();
    let stream = ctx.stream_socket().unwrap();
    stream.attach_actor_gateway(&node).unwrap();
    let session = RoutingId::from(b"game-room-session");

    bind(&stream, &session, &player1);
    bind(&stream, &session, &player2);

    // dispatch 핸들러: 도착한 메시지를 모은다.
    let payloads = Arc::new(Mutex::new(Vec::<String>::new()));
    let sink = Arc::clone(&payloads);
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

    join(&room, &player1);
    join(&room, &player2);

    // 서버가 각 플레이어에게 자기 앞으로 온 메시지를 보낸다.
    send_and_wait(&stream, &session, "player-1", b"your-turn", &payloads, 1);
    send_and_wait(&stream, &session, "player-2", b"wait", &payloads, 2);
    assert_eq!(*payloads.lock().unwrap(), ["your-turn", "wait"]);

    leave(&room, &player1);
    leave(&room, &player2);
    player1.close().unwrap();
    player2.close().unwrap();
    println!("[actor/room] player-1: \"your-turn\", player-2: \"wait\"");
}
// --8<-- [end:doc]
