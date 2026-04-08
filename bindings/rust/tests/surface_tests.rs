//! Surface tests – verify the canonical public API shape.
//!
//! These tests confirm that:
//! - Socket types expose only their specific capabilities
//! - Typed option surfaces exist (no raw option bags)
//! - Monitor canonical surface exists (recv / try_recv)
//! - Blocking/non-blocking naming follows try_* convention

use zlink::*;

// ---------------------------------------------------------------------------
// Socket type capability separation
// ---------------------------------------------------------------------------

#[test]
fn pair_socket_has_send_recv() {
    let ctx = Context::new().unwrap();
    let sock = ctx.pair_socket().unwrap();
    sock.bind("inproc://surface-pair").unwrap();

    // PairSocket exposes: send, try_send, recv, try_recv
    let msg = Message::from_bytes(b"test").unwrap();
    let _ = sock.try_send(msg);
    let _ = sock.try_recv();
}

#[test]
fn pub_socket_has_publish_no_recv() {
    let ctx = Context::new().unwrap();
    let sock = ctx.pub_socket().unwrap();
    sock.bind("inproc://surface-pub").unwrap();

    // PubSocket exposes: publish, try_publish
    let msg = Message::from_bytes(b"payload").unwrap();
    let _ = sock.try_publish("market.price", msg);
    // No recv / try_recv on PubSocket – compile-time enforced
}

#[test]
fn sub_socket_has_subscribe_no_send() {
    let ctx = Context::new().unwrap();
    let sock = ctx.sub_socket().unwrap();
    sock.connect("inproc://surface-sub-target").unwrap();
    sock.set_subscription("").unwrap();

    // SubSocket exposes: subscribe, try_subscribe, set_subscription, unset_subscription
    let _ = sock.try_subscribe();
    // No send on SubSocket – compile-time enforced
}

#[test]
fn discovery_attach_surface_exists_on_supported_sockets() {
    let ctx = Context::new().unwrap();
    let discovery = Discovery::new(&ctx, ServiceType::Socket, "surface-svc").unwrap();
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

    // RouterSocket::send takes (RoutingId, parts)
    let rid = RoutingId::new(b"peer-001").unwrap();
    let msg = Message::from_bytes(b"response").unwrap();
    let _ = sock.try_send(&rid, msg);
}

#[test]
fn stream_socket_send_requires_routing_id() {
    let ctx = Context::new().unwrap();
    let sock = ctx.stream_socket().unwrap();
    sock.bind("tcp://127.0.0.1:*").unwrap();

    let rid = RoutingId::new(b"client-001").unwrap();
    let msg = Message::from_bytes(b"data").unwrap();
    let _ = sock.try_send(&rid, msg);
}

#[test]
fn xpub_socket_has_subscription_event() {
    let ctx = Context::new().unwrap();
    let sock = ctx.xpub_socket().unwrap();
    sock.bind("inproc://surface-xpub").unwrap();

    // XPubSocket: publish, try_publish, receive_subscription_event,
    // try_receive_subscription_event, on_send_ready
    let _ = sock.try_receive_subscription_event();
    let _method = XPubSocket::on_send_ready::<fn()>;
}

#[test]
fn xsub_socket_has_subscribe_no_send() {
    let ctx = Context::new().unwrap();
    let mut sock = ctx.xsub_socket().unwrap();
    sock.connect("inproc://surface-xsub-target").unwrap();
    sock.set_subscription("").unwrap();

    let _ = sock.try_subscribe();
    let _ = sock.sub_options().topics_count();
    sock.on_subscribe(|_topic_message| {}).unwrap();
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
    router.set_handover(false).unwrap();
    router.set_probe(false).unwrap();
    sock.set_routing_id(&RoutingId::new(b"router-surface").unwrap())
        .unwrap();
    common
        .set_linger(std::time::Duration::from_millis(1))
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
}

// ---------------------------------------------------------------------------
// Monitor canonical surface
// ---------------------------------------------------------------------------

#[test]
fn socket_monitor_has_recv_try_recv() {
    let ctx = Context::new().unwrap();
    let sock = ctx.pair_socket().unwrap();
    sock.bind("inproc://surface-monitor").unwrap();

    let mon = SocketMonitor::open(&sock).unwrap();

    // Monitor exposes recv() and try_recv()
    let _ = mon.try_recv();
}

#[test]
fn message_diagnostic_surface_exists() {
    let msg = Message::from_bytes(b"surface").unwrap();
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
