//! Service monitor tests - verify SPOT service monitor surface and
//! blocking receive behavior.

use std::time::{Duration, Instant};

use zlink::*;

fn reserve_tcp_port() -> u16 {
    let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    drop(listener);
    port
}

#[test]
fn spot_open_monitor_exposes_public_service_monitor_surface() {
    let ctx = Context::new().unwrap();
    let node = SpotNode::new(&ctx).unwrap();
    let spot = Spot::new(&node).unwrap();

    let monitor = spot
        .monitor_open(SERVICE_MONITOR_EVENT_SPOT_FILTER_APPLIED)
        .unwrap();

    let _ = monitor.try_recv().unwrap();
}

#[test]
fn service_monitor_recv_blocks_until_subscription_ready_event() {
    let ctx = Context::new().unwrap();
    let port = reserve_tcp_port();
    let endpoint = format!("tcp://127.0.0.1:{port}");

    let publisher_node = SpotNode::new(&ctx).unwrap();
    let subscriber_node = SpotNode::new(&ctx).unwrap();
    let publisher = Spot::new(&publisher_node).unwrap();
    let subscriber = Spot::new(&subscriber_node).unwrap();

    let publisher_monitor = publisher
        .monitor_open(SERVICE_MONITOR_EVENT_SPOT_FIRST_DELIVERY_READY_CHANGED)
        .unwrap();
    let subscriber_monitor = subscriber
        .monitor_open(
            SERVICE_MONITOR_EVENT_SPOT_FILTER_APPLIED
                | SERVICE_MONITOR_EVENT_SPOT_SUBSCRIPTION_READY_CHANGED,
        )
        .unwrap();

    publisher_node.bind(&endpoint).unwrap();
    subscriber_node.connect_peer(&endpoint).unwrap();
    subscriber.set_subscription("room:lobby").unwrap();

    let filter_event = subscriber_monitor.recv().unwrap();
    assert!(filter_event.is_spot_filter_applied());
    assert_eq!(filter_event.subject, "room:lobby");

    let started = Instant::now();
    let ready_event = subscriber_monitor.recv().unwrap();
    assert!(
        started.elapsed() < Duration::from_secs(5),
        "subscription-ready recv timed out"
    );
    assert!(ready_event.is_spot_subscription_ready_changed());
    assert_eq!(ready_event.endpoint, endpoint);

    let pub_snapshot = publisher_monitor.snapshot().unwrap();
    assert!(pub_snapshot.is_send_ready());
}

#[test]
fn service_monitor_try_recv_returns_none_when_no_event_is_pending() {
    let ctx = Context::new().unwrap();
    let node = SpotNode::new(&ctx).unwrap();
    let spot = Spot::new(&node).unwrap();
    let monitor = spot
        .monitor_open(SERVICE_MONITOR_EVENT_SPOT_FILTER_APPLIED)
        .unwrap();

    assert!(monitor.try_recv().unwrap().is_none());
}
