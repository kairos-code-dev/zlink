use std::sync::mpsc;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use zlink::{
    Context, Message, RecvFlags, RecvResult, SendFlags, SpotDispatchEvent, SpotDispatchSubject,
    SpotNode,
};

#[path = "sample_support.rs"]
mod sample_support;

fn accept_next_join(spot: &zlink::Spot) {
    sample_support::wait_until(
        || match spot.recv_actor_join_with_flags(RecvFlags::DONT_WAIT) {
            Ok(Some(request)) => {
                assert_eq!(request.message.as_str().unwrap(), "enter-room");
                spot.reply_actor_join(&request, true, Message::copy_from(b"accepted").unwrap())
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
    let session = zlink::RoutingId::from_bytes(b"room-session");
    let actor_ref = actor.actor_ref().unwrap();
    stream
        .bind_actor(&node, &session, &actor_ref, Duration::from_secs(1))
        .expect("stream actor bind failed");

    let (tx, rx) = mpsc::channel();
    actor
        .join_callback(
            &spot,
            Message::copy_from(b"enter-room").unwrap(),
            move |result| {
                tx.send(result).unwrap();
            },
            SendFlags::DONT_WAIT,
            Duration::from_secs(1),
        )
        .expect("actor join submit failed");
    accept_next_join(&spot);
    let join_result = rx.recv_timeout(Duration::from_secs(2)).unwrap();
    assert_eq!(join_result.unwrap()[0].as_str().unwrap(), "accepted");

    let payload = Arc::new(Mutex::new(None::<String>));
    let payload_cb = Arc::clone(&payload);
    spot.on_dispatch_event(move |info| {
        if info.event != SpotDispatchEvent::ActorReadable {
            return;
        }
        if !matches!(info.subject, SpotDispatchSubject::Actor(_)) {
            return;
        }
        if let Some((_info, part, _more)) = info
            .recv_actor_part_with_flags(RecvFlags::DONT_WAIT)
            .expect("actor dispatch recv failed")
        {
            *payload_cb.lock().unwrap() = Some(part.as_str().unwrap().to_owned());
        }
    })
    .expect("dispatch handler failed");

    stream
        .send_bound_actor_part(
            &node,
            &session,
            "room-player-1",
            Message::copy_from(b"move:north").unwrap(),
            SendFlags::DONT_WAIT,
        )
        .expect("stream actor send failed");

    sample_support::wait_until(
        || payload.lock().unwrap().as_deref() == Some("move:north"),
        Duration::from_secs(2),
        "actor payload",
    );

    actor.leave(&spot).unwrap();
    actor.close().unwrap();
    drop(stream);
}
