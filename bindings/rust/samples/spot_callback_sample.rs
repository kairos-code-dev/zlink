//! SPOT callback sample – demonstrates subscribe callback on a spot facade.

use std::sync::mpsc;
use std::time::Duration;

use zlink::{
    Context, Message, SERVICE_MONITOR_EVENT_CONNECTION_READY,
    SERVICE_MONITOR_EVENT_SPOT_FILTER_APPLIED, Spot, SpotNode,
};

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

    let publisher_monitor = publisher
        .monitor_open(SERVICE_MONITOR_EVENT_CONNECTION_READY)
        .expect("publisher monitor open failed");
    let subscriber_monitor = subscriber
        .monitor_open(
            SERVICE_MONITOR_EVENT_SPOT_FILTER_APPLIED
                | SERVICE_MONITOR_EVENT_CONNECTION_READY,
        )
        .expect("subscriber monitor open failed");

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

    let filter_event = subscriber_monitor.recv().expect("filter event recv failed");
    assert!(
        filter_event.is_spot_filter_applied(),
        "unexpected filter event"
    );
    assert_eq!(filter_event.subject, "room:lobby");

    let ready_event = subscriber_monitor.recv().expect("ready event recv failed");
    assert!(ready_event.is_connection_ready(), "unexpected connection-ready event");

    let pub_snapshot = publisher_monitor
        .snapshot()
        .expect("publisher snapshot failed");
    assert!(pub_snapshot.is_ready(), "publisher spot monitor is not ready");

    publisher
        .publish(
            "room:lobby",
            vec![Message::from_bytes(b"hello-spot").unwrap()],
        )
        .expect("publish failed");

    let (topic, payload) = rx
        .recv_timeout(Duration::from_secs(5))
        .expect("spot callback did not fire within 5s");
    assert_eq!(topic, "room:lobby");
    assert_eq!(payload, "hello-spot");
    println!(
        "[spot/callback] publish: \"room:lobby/hello-spot\" → subscribe: \"{}/{}\"",
        topic, payload
    );
}
