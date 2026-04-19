//! SPOT recv sample – demonstrates peer SPOT publish/subscribe delivery.

use std::time::{Duration, Instant};

#[path = "sample_support.rs"]
mod sample_support;

use zlink::{Context, Discovery, Message, RecvFlags, RecvResult, Registry, ServiceType, SpotNode};

const SERVICE_NAME: &str = "sample";
const TOPIC: &str = "room:lobby";

fn main() {
    let ctx = Context::new().expect("context creation failed");
    let mut registry = Registry::new(&ctx).expect("registry failed");
    let mut discovery =
        Discovery::new(&ctx, ServiceType::Spot, SERVICE_NAME).expect("discovery failed");
    let mut publisher_node = SpotNode::new(&ctx).expect("publisher node failed");
    let mut subscriber_node = SpotNode::new(&ctx).expect("subscriber node failed");
    let registry_pub = sample_support::tcp_endpoint();
    let registry_router = sample_support::tcp_endpoint();
    let publisher_endpoint = sample_support::tcp_endpoint();
    let subscriber_endpoint = sample_support::tcp_endpoint();
    registry
        .bind(&registry_pub, &registry_router)
        .expect("registry bind failed");
    discovery
        .connect_registry(&registry_router)
        .expect("connect_registry failed");
    publisher_node
        .attach_discovery(&discovery)
        .expect("attach_discovery failed");
    subscriber_node
        .attach_discovery(&discovery)
        .expect("attach_discovery failed");
    publisher_node.bind(&publisher_endpoint).expect("bind failed");
    subscriber_node
        .bind(&subscriber_endpoint)
        .expect("bind failed");
    let mut publisher = publisher_node.create_spot().expect("publisher spot failed");
    let mut subscriber = subscriber_node.create_spot().expect("subscriber spot failed");
    subscriber
        .set_subscription(TOPIC)
        .expect("set_subscription failed");

    let deadline = Instant::now() + Duration::from_secs(5);
    while Instant::now() < deadline {
        publisher
            .publish(
                SERVICE_NAME,
                TOPIC,
                Message::copy_from(b"hello-spot").unwrap(),
            )
            .expect("publish failed");
        match subscriber.subscribe_with_flags(RecvFlags::DONT_WAIT) {
            Ok(message) => {
                let payload = message.parts()[0].as_str().unwrap_or("(binary)");
                assert_eq!(message.service_name.as_deref(), Some(SERVICE_NAME));
                assert_eq!(message.topic(), TOPIC);
                assert_eq!(payload, "hello-spot");
                println!(
                    "[spot/recv] service: \"{SERVICE_NAME}\" tick: 1 publish: \"{TOPIC}/hello-spot\" -> recv: \"{}/{}\"",
                    message.topic(),
                    payload
                );
                subscriber.close().expect("subscriber close failed");
                publisher.close().expect("publisher close failed");
                subscriber_node.close().expect("subscriber node close failed");
                publisher_node.close().expect("publisher node close failed");
                discovery.close().expect("discovery close failed");
                registry.close().expect("registry close failed");
                std::process::exit(0);
            }
            Err(err) if err.code() == RecvResult::NoData || err.internal_errno() == libc::ENOENT => {
                std::thread::sleep(Duration::from_millis(10));
            }
            Err(err) => panic!("subscribe failed: {err}"),
        }
    }

    panic!("spot recv did not deliver within 5s");
}
