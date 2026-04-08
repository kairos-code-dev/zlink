//! SPOT recv sample – demonstrates direct subscribe on a spot facade.

use std::time::{Duration, Instant};

mod sample_support;

use zlink::{Context, Message, Spot, SpotNode};

fn main() {
    let ctx = Context::new().expect("context creation failed");

    let publisher_node = SpotNode::new(&ctx).expect("publisher node failed");
    let subscriber_node = SpotNode::new(&ctx).expect("subscriber node failed");
    let publisher = Spot::new(&publisher_node).expect("publisher spot failed");
    let subscriber = Spot::new(&subscriber_node).expect("subscriber spot failed");
    let endpoint = sample_support::tcp_endpoint();

    publisher_node.bind(&endpoint).expect("bind failed");
    subscriber_node
        .connect_peer(&endpoint)
        .expect("connect_peer failed");
    subscriber
        .set_subscription("room:lobby")
        .expect("set_subscription failed");
    sample_support::wait_spot_peer_connected(&subscriber_node, Duration::from_secs(5));

    let deadline = Instant::now() + Duration::from_secs(5);
    let mut message = None;
    while Instant::now() < deadline {
        publisher
            .publish(
                "room:lobby",
                vec![Message::from_bytes(b"hello-spot").unwrap()],
            )
            .expect("publish failed");
        if let Some(received) = subscriber.try_subscribe().expect("try_subscribe failed") {
            message = Some(received);
            break;
        }
        std::thread::yield_now();
    }
    let message = message.expect("spot delivery did not arrive within 5s");
    let payload = message.parts()[0].as_str().unwrap_or("(binary)");
    assert_eq!(message.topic(), "room:lobby");
    assert_eq!(payload, "hello-spot");
    println!(
        "[spot/recv] publish: \"room:lobby/hello-spot\" → subscribe: \"{}/{}\"",
        message.topic(),
        payload
    );
}
