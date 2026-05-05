use std::sync::{Arc, Mutex, mpsc};
use std::time::Duration;

use zlink::{
    ActorAdmissionResult, Context, Message, RecvFlags, RecvResult, SendFlags, SpotDispatchEvent,
    SpotDispatchSubjectKind, SpotNode,
};

#[path = "sample_support.rs"]
mod sample_support;

fn main() {
    let ctx = Context::new().expect("context creation failed");
    let gateway_node = SpotNode::new(&ctx).expect("gateway node failed");
    let mut play_node = SpotNode::new(&ctx).expect("play node failed");
    let mut play_spot = play_node.create_spot().expect("play spot failed");
    play_spot
        .set_routing_id(&zlink::RoutingId::from_bytes(b"play-spot"))
        .unwrap();

    play_node
        .on_actor_admission(|actor_id, message| {
            assert_eq!(actor_id, "play-session-actor");
            assert_eq!(message.as_str().unwrap(), "spawn");
            ActorAdmissionResult::Accept
        })
        .expect("admission handler failed");

    let created = gateway_node
        .create_remote_actor(
            &play_node.routing_id().unwrap(),
            "play-session-actor",
            Message::copy_from(b"spawn").unwrap(),
            Duration::from_secs(1),
        )
        .expect("remote actor create failed");
    assert_eq!(created.actor.actor_id, "play-session-actor");

    let received_payload = Arc::new(Mutex::new(None::<String>));
    let received_payload_cb = Arc::clone(&received_payload);
    play_spot
        .on_dispatch_event(move |info| {
            if info.event == SpotDispatchEvent::ActorReadable
                && info.subject_kind == SpotDispatchSubjectKind::Actor
            {
                if let Some((_recv_info, part, _more)) = info
                    .recv_actor_part_with_flags(RecvFlags::DONT_WAIT)
                    .expect("actor event recv failed")
                {
                    *received_payload_cb.lock().unwrap() = Some(part.as_str().unwrap().to_owned());
                }
            }
        })
        .expect("dispatch handler failed");

    let actor_ref = play_node
        .actor_lookup("play-session-actor")
        .expect("actor lookup failed");
    let (join_tx, join_rx) = mpsc::channel();
    gateway_node
        .join_actor_callback(
            &actor_ref,
            &play_spot.routing_id().unwrap(),
            Message::copy_from(b"join-play").unwrap(),
            move |result| join_tx.send(result).unwrap(),
            SendFlags::DONT_WAIT,
            Duration::from_secs(1),
        )
        .expect("remote actor join submit failed");

    sample_support::wait_until(
        || match play_spot.recv_actor_join_with_flags(RecvFlags::DONT_WAIT) {
            Ok(Some((info, message))) => {
                assert_eq!(message.as_str().unwrap(), "join-play");
                play_spot
                    .reply_actor_join(&info, true, Message::copy_from(b"accepted").unwrap())
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
    join_rx
        .recv_timeout(Duration::from_secs(2))
        .unwrap()
        .unwrap();

    let stream = ctx.stream_socket().expect("stream socket failed");
    let session = zlink::RoutingId::from_bytes(b"gateway-session");
    stream
        .bind_actor(&gateway_node, &session, &actor_ref, Duration::from_secs(1))
        .expect("remote stream actor bind failed");
    stream
        .send_bound_actor_part(
            &gateway_node,
            &session,
            "play-session-actor",
            Message::copy_from(b"client-input").unwrap(),
            SendFlags::DONT_WAIT,
        )
        .expect("gateway relay send failed");

    sample_support::wait_until(
        || received_payload.lock().unwrap().as_deref() == Some("client-input"),
        Duration::from_secs(2),
        "gateway actor payload",
    );

    gateway_node
        .leave_actor(
            &actor_ref,
            &play_spot.routing_id().unwrap(),
            Duration::from_secs(1),
        )
        .unwrap();
    gateway_node
        .destroy_remote_actor(&actor_ref, Duration::from_secs(1))
        .unwrap();
}
