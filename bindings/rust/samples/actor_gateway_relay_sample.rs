use std::sync::{Arc, Mutex, mpsc};
use std::time::Duration;

use zlink::{
    ActorReceived, Context, Message, RecvFlags, RecvResult, SendFlags, SpotDispatchEvent, SpotNode,
};

#[path = "sample_support.rs"]
mod sample_support;

fn main() {
    let ctx = Context::new().expect("context creation failed");
    let gateway_node = SpotNode::new(&ctx).expect("gateway node failed");
    let play_node = SpotNode::new(&ctx).expect("play node failed");
    let mut play_spot = play_node.create_spot().expect("play spot failed");
    play_spot
        .set_routing_id(&zlink::RoutingId::from(b"play-spot"))
        .unwrap();

    let actor = gateway_node
        .create_actor("play-session-actor")
        .expect("actor create failed");

    let received_payload = Arc::new(Mutex::new(None::<String>));
    let received_payload_cb = Arc::clone(&received_payload);
    let actor_received = Arc::new(Mutex::new(ActorReceived::empty()));
    let actor_received_cb = Arc::clone(&actor_received);
    play_spot
        .on_dispatch_event(move |info| {
            if info.event == SpotDispatchEvent::ActorReadable {
                let mut actor_received = actor_received_cb.lock().unwrap();
                if info
                    .recv_actor(&mut actor_received, RecvFlags::DONT_WAIT)
                    .expect("actor event recv failed")
                {
                    *received_payload_cb.lock().unwrap() = Some(
                        actor_received
                            .first_part()
                            .unwrap()
                            .as_str()
                            .unwrap()
                            .to_owned(),
                    );
                }
            }
        })
        .expect("dispatch handler failed");

    let stream = ctx.stream_socket().expect("stream socket failed");
    let session = zlink::RoutingId::from(b"gateway-session");
    let actor_ref = actor.actor_ref().expect("actor ref failed");
    let (bind_tx, bind_rx) = mpsc::channel();
    stream
        .bind_actor(&session, &actor_ref)
        .timeout(Duration::from_secs(1))
        .submit(move |result| bind_tx.send(result).unwrap())
        .expect("remote stream actor bind failed");
    bind_rx
        .recv_timeout(Duration::from_secs(2))
        .unwrap()
        .unwrap();
    let (join_tx, join_rx) = mpsc::channel();
    gateway_node
        .join_actor(
            &actor_ref,
            &play_node.routing_id().unwrap(),
            &play_spot.routing_id().unwrap(),
        )
        .message(Message::try_from(b"join-play").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .timeout(Duration::from_secs(1))
        .submit(move |result, parts| join_tx.send((result, parts)).unwrap())
        .expect("remote actor join submit failed");

    sample_support::wait_until(
        || match play_spot.recv_actor_join_with_flags(RecvFlags::DONT_WAIT) {
            Ok(Some(request)) => {
                assert_eq!(request.message.as_str().unwrap(), "join-play");
                play_spot
                    .reply_actor_join(&request, 0)
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
        "remote actor join request",
    );
    let join_result = join_rx.recv_timeout(Duration::from_secs(2)).unwrap().0;
    assert_eq!(join_result.result, zlink::RequestResult::Ok);

    stream
        .send_bound_actor(&session, "play-session-actor")
        .message(Message::try_from(b"client-input").unwrap())
        .flags(SendFlags::DONT_WAIT)
        .submit()
        .expect("gateway relay send failed");

    sample_support::wait_until(
        || received_payload.lock().unwrap().as_deref() == Some("client-input"),
        Duration::from_secs(2),
        "gateway actor payload",
    );

    println!("[actor/gateway] stream payload: \"client-input\" -> actor: \"client-input\"");
}
