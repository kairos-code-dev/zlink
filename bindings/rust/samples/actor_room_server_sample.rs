use std::sync::mpsc;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use zlink::{
    ActorReceived, Context, Message, RecvFlags, RecvResult, SendFlags, SpotDispatchEvent, SpotNode,
};

#[path = "sample_support.rs"]
mod sample_support;

fn accept_next_join(spot: &zlink::Spot) {
    sample_support::wait_until(
        || match spot.recv_actor_join_with_flags(RecvFlags::DONT_WAIT) {
            Ok(Some(request)) => {
                assert_eq!(request.message.as_str().unwrap(), "enter-room");
                spot.reply_actor_join(&request, 0)
                    .message(Message::try_from(b"accepted").unwrap())
                    .submit()
                    .unwrap();
                true
            }
            Ok(None) => false,
            Err(err) if err.code() == RecvResult::NoData => false,
            Err(err) => panic!("actor join recv failed: {err}"),
        },
        Duration::from_secs(2),
        "actor join request",
    );
}

fn main() {
    let ctx = Context::new().expect("context creation failed");
    let node = SpotNode::new(&ctx).expect("spot node failed");
    let mut spot = node.create_spot().expect("spot failed");
    let mut actor = node
        .create_actor("room-player-1")
        .expect("actor creation failed");
    let stream = ctx.stream_socket().expect("stream socket failed");
    stream
        .attach_actor_gateway(&node)
        .expect("stream actor gateway attach failed");
    let session = zlink::RoutingId::from(b"room-session");

    let (tx, rx) = mpsc::channel();
    actor
        .join(&spot)
        .message(Message::try_from(b"enter-room").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .timeout(Duration::from_secs(1))
        .submit(move |result, parts| {
            tx.send((result, parts)).unwrap();
        })
        .expect("actor join submit failed");
    accept_next_join(&spot);
    let join_result = rx.recv_timeout(Duration::from_secs(2)).unwrap();
    assert_eq!(join_result.0.result, zlink::RequestResult::Ok);
    assert_eq!(join_result.1[0].as_str().unwrap(), "accepted");
    let (bind_tx, bind_rx) = mpsc::channel();
    stream
        .bind_actor(&session, &join_result.0.actor)
        .timeout(Duration::from_secs(1))
        .submit(move |result| bind_tx.send(result).unwrap())
        .expect("stream actor bind failed");
    bind_rx
        .recv_timeout(Duration::from_secs(2))
        .unwrap()
        .unwrap();

    let payload = Arc::new(Mutex::new(None::<String>));
    let payload_cb = Arc::clone(&payload);
    let actor_received = Arc::new(Mutex::new(ActorReceived::empty()));
    let actor_received_cb = Arc::clone(&actor_received);
    spot.on_dispatch_event(move |info| {
        if info.event != SpotDispatchEvent::ActorReadable {
            return;
        }
        let mut actor_received = actor_received_cb.lock().unwrap();
        if info
            .recv_actor(&mut actor_received, RecvFlags::DONT_WAIT)
            .expect("actor dispatch recv failed")
        {
            *payload_cb.lock().unwrap() = Some(
                actor_received
                    .first_part()
                    .unwrap()
                    .as_str()
                    .unwrap()
                    .to_owned(),
            );
        }
    })
    .expect("dispatch handler failed");

    stream
        .send_bound_actor(&session, "room-player-1")
        .message(Message::try_from(b"move:north").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .submit()
        .expect("stream actor send failed");

    sample_support::wait_until(
        || payload.lock().unwrap().as_deref() == Some("move:north"),
        Duration::from_secs(2),
        "actor payload",
    );

    let (leave_tx, leave_rx) = mpsc::channel();
    actor
        .leave(&spot)
        .timeout(Duration::from_secs(1))
        .submit(move |result| leave_tx.send(result).unwrap())
        .unwrap();
    leave_rx
        .recv_timeout(Duration::from_secs(2))
        .unwrap()
        .unwrap();
    actor.close().unwrap();
    drop(stream);
    println!("[actor/room] stream payload: \"move:north\" -> actor: \"move:north\"");
}
