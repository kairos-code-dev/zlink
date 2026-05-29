//! Surface tests – verify the canonical public API shape.
//!
//! These tests confirm that:
//! - Socket types expose only their specific capabilities
//! - Typed option surfaces exist (no raw option bags)
//! - Monitor canonical surface exists (recv)

use zlink::{
    AutoConnectType, Context, Discovery, Message, MonitorEvent, Received, RecvError, RecvFlags,
    RidDuplicatePolicy, RoutingId, SendFlags, SendResult, SocketMonitor, SpotNode, StreamSocket,
    SubmitRetryMode, SubscriptionEvent, TopicMessage, XPubSocket,
};

// ---------------------------------------------------------------------------
// Socket type capability separation
// ---------------------------------------------------------------------------

#[test]
fn pair_socket_has_send_recv() {
    let ctx = Context::new().unwrap();
    let sock = ctx.pair_socket().unwrap();
    sock.bind("inproc://surface-pair").unwrap();

    // PairSocket exposes: send, recv
    let msg = Message::try_from(b"test").unwrap();
    let _ = sock
        .send()
        .message(msg)
        .flags(SendFlags::DONT_WAIT)
        .submit();
    let mut received = Received::empty();
    let _ = sock.recv(&mut received, RecvFlags::DONT_WAIT);
}

#[test]
fn pub_socket_has_publish_no_recv() {
    let ctx = Context::new().unwrap();
    let sock = ctx.pub_socket().unwrap();
    sock.bind("inproc://surface-pub").unwrap();

    // PubSocket exposes: publish
    let msg = Message::try_from(b"payload").unwrap();
    let _ = sock
        .publish("market.price")
        .message(msg)
        .flags(SendFlags::DONT_WAIT)
        .submit();
    // No recv on PubSocket – compile-time enforced
}

#[test]
fn sub_socket_has_subscribe_no_send() {
    let ctx = Context::new().unwrap();
    let sock = ctx.sub_socket().unwrap();
    sock.connect("inproc://surface-sub-target").unwrap();
    sock.set_subscription("").unwrap();

    // SubSocket exposes: subscribe, set_subscription, unset_subscription
    let mut message = TopicMessage::empty();
    let _ = sock.subscribe(&mut message, RecvFlags::DONT_WAIT);
    // No send on SubSocket – compile-time enforced
}

#[test]
fn discovery_attach_surface_exists_on_supported_sockets() {
    let ctx = Context::new().unwrap();
    let discovery = Discovery::new(&ctx, AutoConnectType::ClientServer, "surface-svc").unwrap();
    let dealer = ctx.dealer_socket().unwrap();
    let router = ctx.router_socket().unwrap();
    let pub_sock = ctx.pub_socket().unwrap();
    let sub_sock = ctx.sub_socket().unwrap();

    let _ = dealer.attach_discovery(&discovery);
    let _ = router.attach_discovery(&discovery);
    let _ = pub_sock.attach_discovery(&discovery);
    let _ = sub_sock.attach_discovery(&discovery);
}

#[test]
fn router_socket_send_requires_routing_id() {
    let ctx = Context::new().unwrap();
    let sock = ctx.router_socket().unwrap();
    sock.bind("inproc://surface-router").unwrap();

    // RouterSocket::send takes a RoutingId and returns a builder.
    let rid = RoutingId::from_bytes(b"peer-001");
    let msg = Message::try_from(b"response").unwrap();
    let _ = sock.send(&rid).message(msg).submit();
    let mut received = Received::empty();
    let _ = sock.recv(&mut received, RecvFlags::DONT_WAIT);
}

#[test]
fn stream_socket_send_requires_routing_id() {
    let ctx = Context::new().unwrap();
    let sock = ctx.stream_socket().unwrap();
    sock.bind("tcp://127.0.0.1:*").unwrap();

    let rid = RoutingId::from_bytes(b"client-001");
    let msg = Message::try_from(b"data").unwrap();
    let _ = sock.send(&rid).message(msg).submit();
}

#[test]
fn xpub_socket_has_subscription_event() {
    let ctx = Context::new().unwrap();
    let sock = ctx.xpub_socket().unwrap();
    sock.bind("inproc://surface-xpub").unwrap();

    // XPubSocket: publish, receive_subscription_event, on_send_ready
    let mut event = SubscriptionEvent::empty();
    let _ = sock.receive_subscription_event(&mut event, RecvFlags::DONT_WAIT);
    let _method = XPubSocket::on_send_ready::<fn()>;
}

#[test]
fn xsub_socket_has_subscribe_no_send() {
    let ctx = Context::new().unwrap();
    let sock = ctx.xsub_socket().unwrap();
    sock.connect("inproc://surface-xsub-target").unwrap();
    sock.set_subscription("").unwrap();

    let mut message = TopicMessage::empty();
    let _ = sock.subscribe(&mut message, RecvFlags::DONT_WAIT);
    let _ = sock.sub_options().topics_count();
}

// ---------------------------------------------------------------------------
// Typed option surface exists
// ---------------------------------------------------------------------------

#[test]
fn common_typed_options() {
    let ctx = Context::new().unwrap();
    let sock = ctx.pair_socket().unwrap();
    let options = sock.common_options();

    options.set_send_hwm(1000).unwrap();
    assert_eq!(options.send_hwm().unwrap(), 1000);
    options.set_recv_hwm(2000).unwrap();
    assert_eq!(options.recv_hwm().unwrap(), 2000);

    options
        .set_linger(std::time::Duration::from_millis(100))
        .unwrap();
    assert_eq!(options.submit_retry_mode().unwrap(), SubmitRetryMode::Off);
    assert_eq!(
        options.submit_retry_timeout().unwrap(),
        std::time::Duration::from_millis(0)
    );
    assert_eq!(options.submit_retry_attempts().unwrap(), 0);
    options
        .set_submit_retry_mode(SubmitRetryMode::LocalFailure)
        .unwrap();
    options
        .set_submit_retry_timeout(std::time::Duration::from_millis(42))
        .unwrap();
    options.set_submit_retry_attempts(2).unwrap();
    assert_eq!(
        options.submit_retry_mode().unwrap(),
        SubmitRetryMode::LocalFailure
    );
    assert_eq!(
        options.submit_retry_timeout().unwrap(),
        std::time::Duration::from_millis(42)
    );
    assert_eq!(options.submit_retry_attempts().unwrap(), 2);
    options.set_tcp_keepalive(true).unwrap();
    options.set_tcp_nodelay(true).unwrap();
    options.set_ipv6(false).unwrap();
}

#[test]
fn router_typed_options() {
    let ctx = Context::new().unwrap();
    let sock = ctx.router_socket().unwrap();
    let common = sock.common_options();
    let router = sock.router_options();
    router.set_mandatory(true).unwrap();
    router.set_probe(false).unwrap();
    sock.set_routing_id(&RoutingId::from_bytes(b"router-surface"))
        .unwrap();
    common
        .set_linger(std::time::Duration::from_millis(1))
        .unwrap();
    common
        .set_rid_duplicate_policy(RidDuplicatePolicy::Reject)
        .unwrap();
}

#[test]
fn pub_typed_options() {
    let ctx = Context::new().unwrap();
    let sock = ctx.pub_socket().unwrap();
    let common = sock.common_options();
    let pub_options = sock.pub_options();
    pub_options.set_verbose(false).unwrap();
    pub_options.set_verboser(false).unwrap();
    pub_options.set_nodrop(false).unwrap();
    common
        .set_recv_timeout(std::time::Duration::from_millis(1))
        .unwrap();
}

#[test]
fn stream_typed_options() {
    let ctx = Context::new().unwrap();
    let sock = ctx.stream_socket().unwrap();
    let options = sock.stream_options();
    options.set_notify(true).unwrap();
    assert!(options.notify().unwrap());
    let _set = StreamSocket::set_routing_id;
    let _get = StreamSocket::routing_id;
    let _disconnect_rid = StreamSocket::disconnect_rid;
    let _on_packet = StreamSocket::on_packet::<fn(RoutingId, Message, Message)>;
    let _attach_actor_gateway = StreamSocket::attach_actor_gateway;
}

#[test]
fn rid_disconnect_surface_exists() {
    let ctx = Context::new().unwrap();
    let pair = ctx.pair_socket().unwrap();
    let router = ctx.router_socket().unwrap();
    let node = SpotNode::new(&ctx).unwrap();
    let rid = RoutingId::from_bytes(b"peer-rid");

    let _ = pair.disconnect_rid(&rid);
    let _ = router.disconnect_rid(&rid);
    let _ = node.set_router_bind("inproc://surface-router");
    let _ = node.disconnect_peer_rid(&rid);
    let _ = node.connect_router_channel_peer("", "");
    let _ = node.disconnect_router_channel_peer("", "");
    let _ = node.disconnect_router_channel_peer_rid("", &rid);
}

// ---------------------------------------------------------------------------
// Monitor canonical surface
// ---------------------------------------------------------------------------

#[test]
fn socket_monitor_has_recv() {
    let ctx = Context::new().unwrap();
    let sock = ctx.pair_socket().unwrap();
    sock.bind("inproc://surface-monitor").unwrap();

    let mon = SocketMonitor::open(&sock).unwrap();

    // Monitor exposes recv()
    let _recv: fn(&SocketMonitor) -> Result<MonitorEvent, RecvError> = SocketMonitor::recv;
    let _ignore: fn() -> fn(&MonitorEvent) = SocketMonitor::ignore_handler;
    let _ = mon;
}

#[test]
fn message_diagnostic_surface_exists() {
    let msg = Message::try_from(b"surface").unwrap();
    let _ = msg.ref_count();
    let _ = msg.get_property("missing");
}

// ---------------------------------------------------------------------------
// SendResult is explicit enum, not bool
// ---------------------------------------------------------------------------

#[test]
fn send_result_is_explicit_enum() {
    // Verify SendResult has three distinct variants
    let sent = SendResult::Sent;
    let bp = SendResult::Backpressured;
    let nr = SendResult::NotReady;
    assert!(sent.is_sent());
    assert!(!bp.is_sent());
    assert!(!nr.is_sent());
}
