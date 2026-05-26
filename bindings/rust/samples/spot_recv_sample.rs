//! SPOT recv sample – demonstrates SPOT publish/subscribe delivery.

use std::time::{Duration, Instant};

use zlink::{
    AutoConnectType, Context, Discovery, Message, RecvFlags, RecvResult, Registry,
    RegistryQueryClient, SpotNode, SubmitResult, TopicMessage,
};

#[path = "sample_support.rs"]
mod sample_support;

const SERVICE_NAME: &str = "sample-spot-recv";
const TOPIC: &str = "room:lobby";

fn main() {
    let ctx = Context::new().expect("context creation failed");
    let registry = Registry::new(&ctx).expect("registry failed");
    let publisher_discovery =
        Discovery::new(&ctx, AutoConnectType::SpotMesh, SERVICE_NAME).expect("discovery failed");
    let subscriber_discovery =
        Discovery::new(&ctx, AutoConnectType::SpotMesh, SERVICE_NAME).expect("discovery failed");
    let query = RegistryQueryClient::new(&ctx).expect("query client failed");
    let publisher_node = SpotNode::new(&ctx).expect("publisher node failed");
    let subscriber_node = SpotNode::new(&ctx).expect("subscriber node failed");
    let registry_pub = sample_support::tcp_endpoint();
    let registry_router = sample_support::tcp_endpoint();
    let publisher_endpoint = sample_support::tcp_endpoint();
    let subscriber_endpoint = sample_support::tcp_endpoint();
    registry
        .bind(&registry_pub, &registry_router)
        .expect("registry bind failed");
    registry.set_broadcast_interval(50).unwrap();
    publisher_discovery
        .connect_registry(&registry_router)
        .expect("publisher discovery connect failed");
    subscriber_discovery
        .connect_registry(&registry_router)
        .expect("subscriber discovery connect failed");
    query
        .connect(&registry_router)
        .expect("query connect failed");
    publisher_node
        .attach_discovery(&publisher_discovery)
        .expect("publisher discovery attach failed");
    subscriber_node
        .attach_discovery(&subscriber_discovery)
        .expect("subscriber discovery attach failed");
    publisher_node
        .set_routing_id(&zlink::RoutingId::from_bytes(b"z-rust-spot-recv-publisher"))
        .expect("publisher routing id failed");
    subscriber_node
        .set_routing_id(&zlink::RoutingId::from_bytes(
            b"a-rust-spot-recv-subscriber",
        ))
        .expect("subscriber routing id failed");
    publisher_node
        .set_pub_bind(&publisher_endpoint)
        .expect("publisher bind failed");
    subscriber_node
        .set_pub_bind(&subscriber_endpoint)
        .expect("subscriber bind failed");
    let publisher = publisher_node.create_spot().expect("publisher spot failed");
    let subscriber = subscriber_node
        .create_spot()
        .expect("subscriber spot failed");
    publisher
        .set_routing_id(&zlink::RoutingId::from_bytes(
            b"z-rust-spot-recv-publisher-spot",
        ))
        .expect("publisher spot routing id failed");
    subscriber
        .set_routing_id(&zlink::RoutingId::from_bytes(
            b"a-rust-spot-recv-subscriber-spot",
        ))
        .expect("subscriber spot routing id failed");
    subscriber
        .set_subscription(TOPIC)
        .expect("set_subscription failed");
    sample_support::wait_spot_peer_connected(&publisher_node, Duration::from_secs(5));
    sample_support::wait_spot_peer_connected(&subscriber_node, Duration::from_secs(5));
    sample_support::wait_until(
        || {
            query
                .snapshot(None)
                .map(|entries| {
                    entries
                        .iter()
                        .filter(|entry| entry.channel_name == SERVICE_NAME)
                        .count()
                        >= 2
                })
                .unwrap_or(false)
        },
        Duration::from_secs(5),
        "spot recv registry entries",
    );

    let deadline = Instant::now() + Duration::from_secs(5);
    while Instant::now() < deadline {
        let _ = publisher_node.status_snapshot();
        let _ = subscriber_node.status_snapshot();
        match publisher
            .publish(TOPIC)
            .message(Message::try_from(b"hello-spot").unwrap())
            .submit()
        {
            Ok(_) => {}
            Err(err) if err.code() == SubmitResult::NotFound => {
                std::thread::sleep(Duration::from_millis(10));
                continue;
            }
            Err(err) => panic!("publish failed: {err}"),
        }
        let mut message = TopicMessage::empty();
        match subscriber.subscribe(&mut message, RecvFlags::DONT_WAIT) {
            Ok(true) => {
                let payload = message.parts[0].as_str().unwrap_or("(binary)");
                assert_eq!(message.topic, TOPIC);
                assert_eq!(payload, "hello-spot");
                println!(
                    "[spot/recv] service: \"{SERVICE_NAME}\" tick: 1 publish: \"{TOPIC}/hello-spot\" -> recv: \"{}/{}\"",
                    message.topic, payload
                );
                return;
            }
            Ok(false) => {
                std::thread::sleep(Duration::from_millis(10));
            }
            Err(err)
                if err.code() == RecvResult::NoData || err.internal_errno() == libc::ENOENT =>
            {
                std::thread::sleep(Duration::from_millis(10));
            }
            Err(err) => panic!("subscribe failed: {err}"),
        }
    }

    panic!("spot recv did not deliver within 5s");
}
