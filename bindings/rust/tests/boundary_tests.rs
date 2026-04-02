//! Boundary Validation Tests – verify fail-fast behavior for
//! routing-id length, duration overflow, null checks, etc.

use std::time::Duration;

use zlink::*;

#[test]
fn routing_id_max_length_accepted() {
    let data = vec![0x42u8; 255];
    let rid = RoutingId::new(&data);
    assert!(rid.is_ok(), "255-byte routing id must succeed");
    assert_eq!(rid.unwrap().len(), 255);
}

#[test]
fn routing_id_exceeds_max_fails() {
    let data = vec![0x42u8; 256];
    let rid = RoutingId::new(&data);
    assert!(rid.is_err(), "256-byte routing id must fail immediately");
}

#[test]
fn routing_id_empty_fails() {
    let rid = RoutingId::new(&[]);
    assert!(rid.is_err(), "empty routing id must fail");
}

#[test]
fn routing_id_one_byte_accepted() {
    let rid = RoutingId::new(&[0x01]);
    assert!(rid.is_ok());
    assert_eq!(rid.unwrap().len(), 1);
}

#[test]
fn duration_overflow_fails() {
    // Duration larger than i32::MAX milliseconds
    let huge = Duration::from_millis(i32::MAX as u64 + 1);
    let ctx = Context::new().unwrap();
    let sock = ctx.pair_socket().unwrap();
    let result = sock.set_linger(huge);
    assert!(result.is_err(), "duration overflow must be rejected");
}

#[test]
fn duration_max_accepted() {
    let max_ok = Duration::from_millis(i32::MAX as u64);
    let ctx = Context::new().unwrap();
    let sock = ctx.pair_socket().unwrap();
    let result = sock.set_linger(max_ok);
    assert!(result.is_ok(), "i32::MAX ms duration must be accepted");
}

#[test]
fn null_byte_in_address_rejected() {
    let ctx = Context::new().unwrap();
    let sock = ctx.pair_socket().unwrap();
    let result = sock.bind("inproc://bad\0addr");
    assert!(result.is_err(), "address with null byte must be rejected");
}

#[test]
fn null_byte_in_topic_rejected() {
    let ctx = Context::new().unwrap();
    let pub_sock = ctx.pub_socket().unwrap();
    pub_sock.bind("inproc://bnd-topic-null").unwrap();

    let msg = Message::from_bytes(b"data").unwrap();
    let result = pub_sock.publish("bad\0topic", msg);
    assert!(result.is_err(), "topic with null byte must be rejected");
}

#[test]
fn null_byte_in_subscription_filter_rejected() {
    let ctx = Context::new().unwrap();
    let sub = ctx.sub_socket().unwrap();
    sub.connect("inproc://bnd-filter-null-target").unwrap();

    let result = sub.set_subscription("bad\0filter");
    assert!(result.is_err(), "filter with null byte must be rejected");
}

#[test]
fn message_try_from_bytes() {
    let msg = Message::try_from(&b"hello"[..]);
    assert!(msg.is_ok());
    assert_eq!(msg.unwrap().data(), b"hello");
}

#[test]
fn message_try_from_str() {
    let msg = Message::try_from("world");
    assert!(msg.is_ok());
    assert_eq!(msg.unwrap().as_str().unwrap(), "world");
}

#[test]
fn discovery_service_name_over_255_rejected() {
    let ctx = Context::new().unwrap();
    let too_long = "s".repeat(256);
    let result = Discovery::new(&ctx, ServiceType::Spot, &too_long);
    assert!(
        result.is_err(),
        "service_name over 255 bytes must be rejected"
    );
}

#[test]
fn spot_node_endpoint_over_255_rejected() {
    let ctx = Context::new().unwrap();
    let node = SpotNode::new(&ctx).unwrap();
    let endpoint = format!("tcp://{}", "1".repeat(250));
    let result = node.bind(&endpoint);
    assert!(result.is_err(), "endpoint over 255 bytes must be rejected");
}
