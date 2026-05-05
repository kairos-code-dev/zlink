use std::sync::mpsc;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use zlink::{
    Context, Message, RecvFlags, RecvResult, SendFlags, Spot, SpotDispatchEvent,
    SpotDispatchSubjectKind, SpotNode,
};

#[path = "sample_support.rs"]
mod sample_support;

fn accept_join(spot: &Spot, expected: &'static [u8]) {
    sample_support::wait_until(
        || match spot.recv_actor_join_with_flags(RecvFlags::DONT_WAIT) {
            Ok(Some((info, message))) => {
                assert_eq!(message.as_bytes(), expected);
                spot.reply_actor_join(&info, true, Message::copy_from(b"accepted").unwrap())
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
    let first_spot = node.create_spot().expect("first spot failed");
    let mut second_spot = node.create_spot().expect("second spot failed");
    let mut actor = node
        .create_actor("single-player")
        .expect("actor creation failed");

    let (first_tx, first_rx) = mpsc::channel();
    actor
        .join_callback(
            &first_spot,
            Message::copy_from(b"join-first").unwrap(),
            move |result| first_tx.send(result).unwrap(),
            SendFlags::DONT_WAIT,
            Duration::from_secs(1),
        )
        .expect("first join submit failed");
    accept_join(&first_spot, b"join-first");
    first_rx
        .recv_timeout(Duration::from_secs(2))
        .unwrap()
        .unwrap();

    let stream = ctx.stream_socket().expect("stream socket failed");
    let session = zlink::RoutingId::from_bytes(b"single-player-session");
    let actor_ref = actor.actor_ref().unwrap();
    stream
        .bind_actor(&node, &session, &actor_ref, Duration::from_secs(1))
        .expect("stream actor bind failed");
    stream
        .send_bound_actor_part(
            &node,
            &session,
            "single-player",
            Message::copy_from(b"before").unwrap(),
            SendFlags::DONT_WAIT,
        )
        .expect("before send failed");
    actor.leave(&first_spot).unwrap();
    stream
        .send_bound_actor_part(
            &node,
            &session,
            "single-player",
            Message::copy_from(b"between").unwrap(),
            SendFlags::DONT_WAIT,
        )
        .expect("between send failed");

    let payloads = Arc::new(Mutex::new(Vec::<String>::new()));
    let payloads_cb = Arc::clone(&payloads);
    second_spot
        .on_dispatch_event(move |info| {
            if info.event != SpotDispatchEvent::ActorReadable
                || info.subject_kind != SpotDispatchSubjectKind::Actor
            {
                return;
            }
            loop {
                match info.recv_actor_part_with_flags(RecvFlags::DONT_WAIT) {
                    Ok(Some((_info, part, _more))) => payloads_cb
                        .lock()
                        .unwrap()
                        .push(part.as_str().unwrap().to_owned()),
                    Ok(None) => break,
                    Err(_) => break,
                }
            }
        })
        .expect("dispatch handler failed");

    let (second_tx, second_rx) = mpsc::channel();
    actor
        .join_callback(
            &second_spot,
            Message::copy_from(b"join-second").unwrap(),
            move |result| second_tx.send(result).unwrap(),
            SendFlags::DONT_WAIT,
            Duration::from_secs(1),
        )
        .expect("second join submit failed");
    accept_join(&second_spot, b"join-second");
    second_rx
        .recv_timeout(Duration::from_secs(2))
        .unwrap()
        .unwrap();

    sample_support::wait_until(
        || payloads.lock().unwrap().len() >= 2,
        Duration::from_secs(2),
        "queued actor payloads",
    );
    assert_eq!(*payloads.lock().unwrap(), ["before", "between"]);

    actor.leave(&second_spot).unwrap();
    actor.close().unwrap();
}
