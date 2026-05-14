//! Single DEALER/ROUTER throughput/latency benchmark.

mod common;

use std::time::Duration;
use zlink::*;

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let Some(bind_endpoint) = common::resolve_endpoint_or_emit_unsupported(
        "DEALER_ROUTER",
        &config.transport,
        "dealer-router",
    ) else {
        return;
    };

    let ctx = common::perf_context();
    let router = ctx.router_socket().expect("router");
    let dealer = ctx.dealer_socket().expect("dealer");
    let rid = RoutingId::from_bytes(b"perf-dealer");
    dealer.set_routing_id(&rid).expect("set rid");
    router
        .common_options()
        .set_send_hwm(common::resolve_single_send_hwm())
        .expect("router sndhwm");
    router
        .common_options()
        .set_recv_hwm(common::resolve_single_recv_hwm())
        .expect("router rcvhwm");
    dealer
        .common_options()
        .set_send_hwm(common::resolve_single_send_hwm())
        .expect("dealer sndhwm");
    dealer
        .common_options()
        .set_recv_hwm(common::resolve_single_recv_hwm())
        .expect("dealer rcvhwm");
    // PERF_SINGLE_TEST_POLICY § 1.4: receiver blocks on `recv()` until the
    // wire-level stop token arrives, so no recv timeout is needed.

    if matches!(config.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        common::setup_raw_tls_server(&router, &tls).expect("router tls");
        common::setup_raw_tls_client(&dealer, &tls).expect("dealer tls");
    }

    let mut router_mon = SocketMonitor::open(&router).expect("router monitor");
    let mut mon = SocketMonitor::open(&dealer).expect("monitor");
    if let Err(err) = router.bind(&bind_endpoint) {
        if common::handle_transport_setup_error("DEALER_ROUTER", &config.transport, "bind", err) {
            return;
        }
        panic!("bind: {err}");
    }
    let endpoint = router.last_endpoint().unwrap_or(bind_endpoint);
    if let Err(err) = dealer.connect(&endpoint) {
        if common::handle_transport_setup_error("DEALER_ROUTER", &config.transport, "connect", err)
        {
            return;
        }
        panic!("connect: {err}");
    }
    let ready_timeout = common::resolve_single_ready_timeout();
    common::wait_monitor_ready(&mut router_mon, ready_timeout, "dealer-router router");
    common::wait_monitor_ready(&mut mon, ready_timeout, "dealer-router dealer");
    dealer
        .send()
        .message(Message::copy_from(b"PING").expect("dealer ping"))
        .submit()
        .expect("dealer handshake send");
    let mut handshake = zlink::Received::empty();
    if let Err(err) = router.recv(&mut handshake, zlink::RecvFlags::NONE) {
        panic!("router handshake recv: {err}");
    }
    let reply_rid = handshake
        .routing_id()
        .expect("router handshake rid")
        .clone();
    assert_eq!(handshake.parts()[0].as_bytes(), b"PING");
    router
        .send(&reply_rid)
        .message(Message::copy_from(b"PONG").expect("router pong"))
        .submit()
        .expect("router handshake reply");
    let mut handshake_reply = zlink::Received::empty();
    if let Err(err) = dealer.recv(&mut handshake_reply, zlink::RecvFlags::NONE) {
        panic!("dealer handshake recv: {err}");
    }
    assert_eq!(handshake_reply.parts()[0].as_bytes(), b"PONG");

    let collector = common::MetricCollector::new();
    let stats = collector.shared();
    let active = Duration::from_secs(config.duration_seconds);
    let active_deadline = std::time::Instant::now() + active;
    let send_thread = std::thread::spawn(move || {
        common::send_loop(
            active_deadline,
            config.size,
            common::PHASE_ACTIVE,
            |msg| match dealer
                .send()
                .message(msg)
                .flags(zlink::SendFlags::DONT_WAIT)
                .submit()
            {
                Ok(sent) => sent,
                Err(err) if err.code() == SubmitResult::NotConnected => false,
                Err(err) => panic!("active send: {err}"),
            },
        );
        common::send_stop_token(|msg| {
            dealer
                .send()
                .message(msg)
                .submit()
                .map(|_| ())
                .map_err(Into::into)
        });
    });

    let mut received = zlink::Received::empty();
    loop {
        match router.recv(&mut received, zlink::RecvFlags::NONE) {
            Ok(true) => {
                let data = common::message_payload(received.parts());
                if common::is_stop_token(data) {
                    break;
                }
                common::handle_recv(data, config.size, &stats, active_deadline);
            }
            Ok(false) => continue,
            Err(err) => panic!("dealer-router router recv failed: {err}"),
        }
    }
    send_thread.join().expect("sender thread");

    let result = collector.finish();
    common::print_result(
        "DEALER_ROUTER",
        &config.transport,
        config.size,
        config.duration_seconds,
        &result,
    );
}
