// 자립형 가이드 예제: SPOT 토픽 pub/sub.
// 한 노드가 토픽에 publish하면, 그 토픽을 구독한 다른 노드가 받는다.
//   cargo run --example spot_pubsub_example

use std::thread::sleep;
use std::time::{Duration, Instant};

use zlink::{Context, Message, RecvFlags, SpotNode, TopicMessage};

#[path = "sample_support.rs"]
mod sample_support;

fn main() {
    // --8<-- [start:doc]
    let ctx = Context::new().unwrap();
    // 토픽을 발행하는 노드와 구독하는 노드.
    let publisher_node = SpotNode::new(&ctx).unwrap();
    let subscriber_node = SpotNode::new(&ctx).unwrap();

    let topic = "room:lobby";
    let pub_endpoint = sample_support::tcp_endpoint();
    let sub_endpoint = sample_support::tcp_endpoint();
    publisher_node.set_pub_bind(&pub_endpoint).unwrap();
    subscriber_node.set_pub_bind(&sub_endpoint).unwrap();
    publisher_node.connect_peer(&sub_endpoint).unwrap();
    subscriber_node.connect_peer(&pub_endpoint).unwrap();

    let publisher = publisher_node.create_spot().unwrap();
    let subscriber = subscriber_node.create_spot().unwrap();
    // 구독자는 받을 토픽을 등록한다.
    subscriber.set_subscription(topic).unwrap();
    sample_support::wait_spot_peer_connected(&publisher_node, Duration::from_secs(15));
    sample_support::wait_spot_peer_connected(&subscriber_node, Duration::from_secs(15));

    // 연결 직후 첫 publish가 닿기 전일 수 있어, 도착할 때까지 반복 발행한다.
    let mut received = TopicMessage::empty();
    let mut delivered = false;
    let deadline = Instant::now() + Duration::from_secs(5);
    while Instant::now() < deadline {
        publisher
            .publish(topic)
            .message(Message::try_from(b"hello-everyone").unwrap())
            .submit()
            .unwrap();
        if let Ok(true) = subscriber.subscribe(&mut received, RecvFlags::DONT_WAIT) {
            delivered = true;
            break;
        }
        sleep(Duration::from_millis(10));
    }
    assert!(delivered, "spot delivery did not arrive");

    println!(
        "[spot/pubsub] topic \"{}\" -> recv: \"{}\"",
        received.topic(),
        received.first_part().unwrap().as_str().unwrap()
    );
    // --8<-- [end:doc]
}
