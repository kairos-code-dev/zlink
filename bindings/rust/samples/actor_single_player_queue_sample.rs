use std::sync::mpsc;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use zlink::{
    ActorReceived, Context, Message, RecvFlags, RecvResult, SendFlags, Spot, SpotDispatchEvent,
    SpotNode,
};

#[path = "sample_support.rs"]
mod sample_support;

fn accept_join(spot: &Spot, expected: &'static [u8]) {
    sample_support::wait_until(
        || match spot.recv_actor_join_with_flags(RecvFlags::DONT_WAIT) {
            Ok(Some(request)) => {
                assert_eq!(request.message.as_bytes(), expected);
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
    let first_spot = node.create_spot().expect("first spot failed");
    let mut second_spot = node.create_spot().expect("second spot failed");
    let mut actor = node
        .create_actor("single-player")
        .expect("actor creation failed");
    let stream = ctx.stream_socket().expect("stream socket failed");
    stream
        .attach_actor_gateway(&node)
        .expect("stream actor gateway attach failed");
    let session = zlink::RoutingId::from(b"single-player-session");

    let (first_tx, first_rx) = mpsc::channel();
    actor
        .join(&first_spot)
        .message(Message::try_from(b"join-first").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .timeout(Duration::from_secs(1))
        .submit(move |result, parts| first_tx.send((result, parts)).unwrap())
        .expect("first join submit failed");
    accept_join(&first_spot, b"join-first");
    let first_join = first_rx.recv_timeout(Duration::from_secs(2)).unwrap().0;
    assert_eq!(first_join.result, zlink::RequestResult::Ok);
    let (bind_tx, bind_rx) = mpsc::channel();
    stream
        .bind_actor(&session, &first_join.actor)
        .timeout(Duration::from_secs(1))
        .submit(move |result| bind_tx.send(result).unwrap())
        .expect("stream actor bind failed");
    bind_rx
        .recv_timeout(Duration::from_secs(2))
        .unwrap()
        .unwrap();

    stream
        .send_bound_actor(&session, "single-player")
        .message(Message::try_from(b"before").unwrap())
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
        .send_bound_actor(&session, "single-player")
        .message(Message::try_from(b"between").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .submit()
        .expect("between send failed");

    let payloads = Arc::new(Mutex::new(Vec::<String>::new()));
    let payloads_cb = Arc::clone(&payloads);
    let actor_received = Arc::new(Mutex::new(ActorReceived::empty()));
    let actor_received_cb = Arc::clone(&actor_received);
    second_spot
        .on_dispatch_event(move |info| {
            if info.event != SpotDispatchEvent::ActorReadable {
                return;
            }
            let mut actor_received = actor_received_cb.lock().unwrap();
            loop {
                match info.recv_actor(&mut actor_received, RecvFlags::DONT_WAIT) {
                    Ok(true) => payloads_cb.lock().unwrap().push(
                        actor_received
                            .first_part()
                            .unwrap()
                            .as_str()
                            .unwrap()
                            .to_owned(),
                    ),
                    Ok(false) => break,
                    Err(_) => break,
                }
            }
        })
        .expect("dispatch handler failed");

    let (second_tx, second_rx) = mpsc::channel();
    actor
        .join(&second_spot)
        .message(Message::try_from(b"join-second").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .timeout(Duration::from_secs(1))
        .submit(move |result, parts| second_tx.send((result, parts)).unwrap())
        .expect("second join submit failed");
    accept_join(&second_spot, b"join-second");
    second_rx.recv_timeout(Duration::from_secs(2)).unwrap();

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
