//! SPOT recv sample – demonstrates dispatcher callback plus recv.

use std::sync::mpsc;
use std::time::{Duration, Instant};

mod sample_support;

use zlink::{Context, Message, SpotDispatchEvent, SpotNode};

const SERVICE_NAME: &str = "sample";
const TOPIC: &str = "room:lobby";

fn attach_service_pair(
    ctx: &Context,
    node: &SpotNode,
    service_name: &str,
) -> (zlink::PubSocket, zlink::SubSocket) {
    let pub_sock = ctx.pub_socket().expect("pub socket failed");
    let sub_sock = ctx.sub_socket().expect("sub socket failed");
    let endpoint = sample_support::tcp_endpoint();
    pub_sock.bind(&endpoint).expect("pub bind failed");
    sub_sock.connect(&endpoint).expect("sub connect failed");
    node.attach_pubsub(service_name, &pub_sock, &sub_sock)
        .expect("attach_pubsub failed");
    (pub_sock, sub_sock)
}

fn main() {
    let ctx = Context::new().expect("context creation failed");
    let node = SpotNode::new(&ctx).expect("spot node failed");
    let _service = attach_service_pair(&ctx, &node, SERVICE_NAME);
    let mut spot = node.create_spot().expect("spot failed");

    let (tx, rx) = mpsc::channel();
    spot.on_dispatch_event(move |event| {
        if event == SpotDispatchEvent::SubscribeReadable {
            let _ = tx.send(());
        }
    })
    .expect("on_dispatch_event failed");

    spot.set_subscription(TOPIC)
        .expect("set_subscription failed");

    let deadline = Instant::now() + Duration::from_secs(5);
    while Instant::now() < deadline {
        spot.publish(
            SERVICE_NAME,
            TOPIC,
            vec![Message::copy_from(b"hello-spot").unwrap()],
        )
        .expect("publish failed");

        if rx.recv_timeout(Duration::from_millis(50)).is_err() {
            continue;
        }

        let message = spot.subscribe().expect("subscribe failed");
        let payload = message.parts()[0].as_str().unwrap_or("(binary)");
        assert_eq!(message.service_name.as_deref(), Some(SERVICE_NAME));
        assert_eq!(message.topic(), TOPIC);
        assert_eq!(payload, "hello-spot");
        println!(
            "[spot/recv] service: \"{SERVICE_NAME}\" tick: 1 publish: \"{TOPIC}/hello-spot\" -> recv: \"{}/{}\"",
            message.topic(),
            payload
        );
        return;
    }

    panic!("spot recv did not deliver within 5s");
}
