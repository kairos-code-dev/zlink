//! SPOT recv sample – demonstrates service-aware publish/subscribe.

use std::time::{Duration, Instant};

mod sample_support;

use zlink::{Context, Message, SpotNode};

const SERVICE_NAME: &str = "spot-svc";
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

    let publisher_node = SpotNode::new(&ctx).expect("publisher node failed");
    let subscriber_node = SpotNode::new(&ctx).expect("subscriber node failed");
    let publisher = publisher_node.create_spot().expect("publisher spot failed");
    let subscriber = subscriber_node
        .create_spot()
        .expect("subscriber spot failed");
    let endpoint = sample_support::tcp_endpoint();

    let _publisher_service = attach_service_pair(&ctx, &publisher_node, SERVICE_NAME);
    let _subscriber_service = attach_service_pair(&ctx, &subscriber_node, SERVICE_NAME);

    publisher_node.bind(&endpoint).expect("bind failed");
    subscriber_node
        .connect_peer(&endpoint)
        .expect("connect_peer failed");
    subscriber
        .set_subscription(TOPIC)
        .expect("set_subscription failed");
    sample_support::wait_spot_peer_connected(&subscriber_node, Duration::from_secs(5));

    let deadline = Instant::now() + Duration::from_secs(5);
    let mut message = None;
    while Instant::now() < deadline {
        publisher
            .publish(
                SERVICE_NAME,
                TOPIC,
                vec![Message::copy_from(b"hello-spot").unwrap()],
            )
            .expect("publish failed");
        message = Some(subscriber.subscribe().expect("subscribe failed"));
        break;
    }
    let message = message.expect("spot delivery did not arrive within 5s");
    let payload = message.parts()[0].as_str().unwrap_or("(binary)");
    assert_eq!(message.service_name.as_deref(), Some(SERVICE_NAME));
    assert_eq!(message.topic(), TOPIC);
    assert_eq!(payload, "hello-spot");
    println!(
        "[spot/recv] publish: \"{SERVICE_NAME}/{TOPIC}/hello-spot\" → subscribe: \"{}/{}\"",
        message.topic(),
        payload
    );
}
