use std::sync::mpsc;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use zlink::{
    Context, Message, RecvFlags, RecvResult, SendFlags, Spot, SpotDispatchEvent,
    SpotDispatchSubject, SpotNode,
};

#[path = "sample_support.rs"]
mod sample_support;

fn accept_join(spot: &Spot, expected: &'static [u8]) {
    sample_support::wait_until(
        || match spot.recv_actor_join_with_flags(RecvFlags::DONT_WAIT) {
            Ok(Some(request)) => {
                assert_eq!(request.message.as_bytes(), expected);
                spot.reply_actor_join(&request, true)
                    .message(Message::copy_from(b"accepted").unwrap())
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
    let first_spot = node.create_spot().expect("first spot failed");
    let mut second_spot = node.create_spot().expect("second spot failed");
    let mut actor = node
        .create_actor("single-player")
        .expect("actor creation failed");
    let stream = ctx.stream_socket().expect("stream socket failed");
    let session = zlink::RoutingId::from_bytes(b"single-player-session");
    let actor_ref = actor.actor_ref().unwrap();
    let (bind_tx, bind_rx) = mpsc::channel();
    stream
        .bind_actor(&session, &actor_ref)
        .timeout(Duration::from_secs(1))
        .submit(move |result| bind_tx.send(result).unwrap())
        .expect("stream actor bind failed");
    bind_rx
        .recv_timeout(Duration::from_secs(2))
        .unwrap()
        .unwrap();

    let (first_tx, first_rx) = mpsc::channel();
    actor
        .join(&first_spot)
        .message(Message::copy_from(b"join-first").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .timeout(Duration::from_secs(1))
        .submit(move |result, parts| first_tx.send((result, parts)).unwrap())
        .expect("first join submit failed");
    accept_join(&first_spot, b"join-first");
    first_rx.recv_timeout(Duration::from_secs(2)).unwrap().0;

    stream
        .send_bound_actor_part(&session, "single-player")
        .message(Message::copy_from(b"before").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .submit()
        .expect("before send failed");
    let (leave_tx, leave_rx) = mpsc::channel();
    actor
        .leave(&first_spot)
        .timeout(Duration::from_secs(1))
        .submit(move |result| leave_tx.send(result).unwrap())
        .unwrap();
    leave_rx
        .recv_timeout(Duration::from_secs(2))
        .unwrap()
        .unwrap();
    stream
        .send_bound_actor_part(&session, "single-player")
        .message(Message::copy_from(b"between").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .submit()
        .expect("between send failed");

    let payloads = Arc::new(Mutex::new(Vec::<String>::new()));
    let payloads_cb = Arc::clone(&payloads);
    second_spot
        .on_dispatch_event(move |info| {
            if info.event != SpotDispatchEvent::ActorReadable {
                return;
            }
            if !matches!(info.subject, SpotDispatchSubject::Actor(_)) {
                return;
            }
            loop {
                match info.recv_actor_part_with_flags(RecvFlags::DONT_WAIT) {
                    Ok(Some(part)) => payloads_cb
                        .lock()
                        .unwrap()
                        .push(part.message.as_str().unwrap().to_owned()),
                    Ok(None) => break,
                    Err(_) => break,
                }
            }
        })
        .expect("dispatch handler failed");

    let (second_tx, second_rx) = mpsc::channel();
    actor
        .join(&second_spot)
        .message(Message::copy_from(b"join-second").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .timeout(Duration::from_secs(1))
        .submit(move |result, parts| second_tx.send((result, parts)).unwrap())
        .expect("second join submit failed");
    accept_join(&second_spot, b"join-second");
    second_rx.recv_timeout(Duration::from_secs(2)).unwrap().0;

    sample_support::wait_until(
        || payloads.lock().unwrap().len() >= 2,
        Duration::from_secs(2),
        "queued actor payloads",
    );
    assert_eq!(*payloads.lock().unwrap(), ["before", "between"]);

    let (leave_tx, leave_rx) = mpsc::channel();
    actor
        .leave(&second_spot)
        .timeout(Duration::from_secs(1))
        .submit(move |result| leave_tx.send(result).unwrap())
        .unwrap();
    leave_rx
        .recv_timeout(Duration::from_secs(2))
        .unwrap()
        .unwrap();
    actor.close().unwrap();
    println!(
        "[actor/single-player] queued payload: \"before/between\" -> actor: \"before/between\""
    );
}
