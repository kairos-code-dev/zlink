//! SPOT recv sample - demonstrates dispatcher callback plus recv.

use std::sync::mpsc;
use std::time::Duration;

mod sample_support;

use zlink::{Context, Message, RecvFlags, SpotDispatchEvent, SpotNode};

const SERVICE_NAME: &str = "sample";
const TOPIC: &str = "room:lobby";
const PAYLOAD: &str = "hello-spot";

fn main() {
    let ctx = Context::new().expect("context creation failed");
    let node = SpotNode::new(&ctx).expect("spot node failed");
    let mut spot = node.create_spot().expect("spot creation failed");
    let pub_sock = ctx.pub_socket().expect("pub socket failed");
    let sub_sock = ctx.sub_socket().expect("sub socket failed");
    let endpoint = sample_support::tcp_endpoint();
    pub_sock.bind(&endpoint).expect("pub bind failed");
    sub_sock.connect(&endpoint).expect("sub connect failed");

    sample_support::wait_until(
        || node.attach_pubsub(SERVICE_NAME, &pub_sock, &sub_sock).is_ok(),
        Duration::from_secs(5),
        "spot service attachment",
    );

    let (tx, rx) = mpsc::channel();
    spot.on_dispatch_event_with_context(move |event, dispatch| {
            if event == SpotDispatchEvent::SubscribeReadable {
                let result = dispatch
                    .subscribe_with_flags(RecvFlags::DONT_WAIT)
                    .map(|message| {
                        let service_name = message.service_name.as_deref().unwrap_or("").to_string();
                        let topic = message.topic().to_string();
                        let payload = message.parts()[0].as_str().unwrap_or("(binary)").to_string();
                        (service_name, topic, payload)
                    });
                let _ = tx.send(result);
            }
        })
        .expect("on_dispatch_event_with_context failed");
    spot.set_subscription(TOPIC)
        .expect("set_subscription failed");

    spot.publish(
        SERVICE_NAME,
        TOPIC,
        vec![Message::copy_from(PAYLOAD.as_bytes()).unwrap()],
    )
    .expect("publish failed");

    let (service_name, topic, payload) = rx
        .recv_timeout(Duration::from_secs(5))
        .expect("spot dispatch callback did not deliver within 5s")
        .expect("callback subscribe failed");

    assert_eq!(service_name, SERVICE_NAME);
    assert_eq!(topic, TOPIC);
    assert_eq!(payload, PAYLOAD);
    println!(
        "[spot/recv] service: \"{SERVICE_NAME}\" tick: 1 publish: \"{TOPIC}/{PAYLOAD}\" -> recv: \"{topic}/{payload}\""
    );
}
