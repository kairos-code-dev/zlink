//! SPOT callback sample – demonstrates subscribe callback on a spot facade.

use std::sync::mpsc;
use std::time::{Duration, Instant};

use zlink::{Context, Message, Spot, SpotNode};

fn wait_until<F>(mut predicate: F, timeout: Duration, description: &str)
where
    F: FnMut() -> bool,
{
    let deadline = Instant::now() + timeout;
    while Instant::now() < deadline {
        if predicate() {
            return;
        }
        std::thread::yield_now();
    }
    panic!("timed out waiting for {description}");
}

fn main() {
    let ctx = Context::new().expect("context creation failed");

    let publisher_node = SpotNode::new(&ctx).expect("publisher node failed");
    let subscriber_node = SpotNode::new(&ctx).expect("subscriber node failed");
    let publisher = Spot::new(&publisher_node).expect("publisher spot failed");
    let mut subscriber = Spot::new(&subscriber_node).expect("subscriber spot failed");

    let (tx, rx) = mpsc::channel();
    subscriber
        .on_subscribe(move |message| {
            let payload = message.parts()[0]
                .as_str()
                .unwrap_or("(binary)")
                .to_string();
            let _ = tx.send((message.topic().to_string(), payload));
        })
        .expect("on_subscribe failed");

    publisher_node
        .bind("tcp://127.0.0.1:0")
        .expect("bind failed");
    let endpoint = publisher_node
        .last_endpoint()
        .expect("last_endpoint failed");
    subscriber_node
        .connect_peer(&endpoint)
        .expect("connect_peer failed");
    subscriber
        .set_subscription("room:lobby")
        .expect("set_subscription failed");

    wait_until(
        || {
            subscriber_node
                .status_snapshot()
                .map(|status| status.connected_peer_count > 0)
                .unwrap_or(false)
        },
        Duration::from_secs(5),
        "spot peer connection",
    );

    let deadline = Instant::now() + Duration::from_secs(5);
    while Instant::now() < deadline {
        publisher
            .publish(
                "room:lobby",
                vec![Message::from_bytes(b"hello-spot").unwrap()],
            )
            .expect("publish failed");
        if let Ok((topic, payload)) = rx.recv_timeout(Duration::from_millis(50)) {
            assert_eq!(topic, "room:lobby");
            assert_eq!(payload, "hello-spot");
            println!(
                "[spot/callback] publish: \"room:lobby/hello-spot\" → subscribe: \"{}/{}\"",
                topic, payload
            );
            return;
        }
        std::thread::yield_now();
    }

    panic!("spot callback did not fire within 5s");
}
