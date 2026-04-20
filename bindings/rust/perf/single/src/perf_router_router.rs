//! Single ROUTER/ROUTER throughput/latency benchmark.

mod common;

use std::time::Duration;
use zlink::*;

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let Some(bind_endpoint) = common::resolve_endpoint_or_emit_unsupported(
        "ROUTER_ROUTER",
        &config.transport,
        "router-router",
    ) else {
        return;
    };

    let ctx = Context::new().expect("context");
    let receiver = ctx.router_socket().expect("receiver");
    let sender = ctx.router_socket().expect("sender");
    receiver
        .common_options()
        .set_send_hwm(common::resolve_single_send_hwm())
        .expect("receiver sndhwm");
    receiver
        .common_options()
        .set_recv_hwm(common::resolve_single_recv_hwm())
        .expect("receiver rcvhwm");
    sender
        .common_options()
        .set_send_hwm(common::resolve_single_send_hwm())
        .expect("sender sndhwm");
    sender
        .common_options()
        .set_recv_hwm(common::resolve_single_recv_hwm())
        .expect("sender rcvhwm");
    sender
        .common_options()
        .set_send_timeout(common::resolve_single_send_timeout())
        .expect("sender sndtimeo");
    sender
        .common_options()
        .set_recv_timeout(common::resolve_single_send_timeout())
        .expect("sender rcvtimeo");
    receiver
        .common_options()
        .set_recv_timeout(common::resolve_single_send_timeout())
        .expect("receiver rcvtimeo");

    let sender_rid = RoutingId::from_bytes(b"perf-rr-sender");
    sender.set_routing_id(&sender_rid).expect("set rid");
    let receiver_rid = RoutingId::from_bytes(b"perf-rr-receiver");
    receiver.set_routing_id(&receiver_rid).expect("set rid");
    receiver
        .router_options()
        .set_mandatory(true)
        .expect("receiver mandatory");
    sender
        .router_options()
        .set_mandatory(true)
        .expect("sender mandatory");
    sender
        .router_options()
        .set_connect_routing_id(&receiver_rid)
        .expect("connect rid");

    if matches!(config.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        common::setup_raw_tls_server(&receiver, &tls).expect("receiver tls");
        common::setup_raw_tls_client(&sender, &tls).expect("sender tls");
    }

    let receiver_mon = SocketMonitor::open(&receiver).expect("receiver monitor");
    let mon = SocketMonitor::open(&sender).expect("monitor");
    if let Err(err) = receiver.bind(&bind_endpoint) {
        if common::handle_transport_setup_error("ROUTER_ROUTER", &config.transport, "bind", err) {
            return;
        }
        panic!("bind: {err}");
    }
    let endpoint = receiver.last_endpoint().unwrap_or(bind_endpoint);
    if let Err(err) = sender.connect(&endpoint) {
        if common::handle_transport_setup_error("ROUTER_ROUTER", &config.transport, "connect", err)
        {
            return;
        }
        panic!("connect: {err}");
    }
    let ready_timeout = common::resolve_single_ready_timeout();
    let target = receiver_rid.clone();
    common::wait_monitor_ready(&receiver_mon, ready_timeout, "router-router receiver");
    common::wait_monitor_ready(&mon, ready_timeout, "router-router sender");
    if config.transport == "inproc" {
        std::thread::sleep(Duration::from_millis(50));
    } else {
        common::wait_send_probe_ready(
            "router/router perf endpoint",
            config.size,
            ready_timeout,
            |msg| sender.send(&target, msg),
            || {
                let mut saw_probe = false;
                loop {
                    match receiver.recv_with_flags(RecvFlags::DONT_WAIT) {
                        Ok(received) => {
                            let data = common::message_payload(received.parts());
                            if common::is_valid_message(data, config.size) {
                                saw_probe = true;
                            }
                        }
                        Err(err) if err.code() == RecvResult::NoData => break,
                        Err(_) => break,
                    }
                }
                saw_probe
            },
        );
    }
    sender
        .send(&target, Message::copy_from(b"PING").expect("router ping"))
        .expect("router handshake send");
    let handshake = match receiver.recv() {
        Ok(received) => received,
        Err(err) => {
            if common::handle_local_route_handshake_error(
                "ROUTER_ROUTER",
                &config.transport,
                "handshake_recv",
                err,
            ) {
                return;
            }
            panic!("receiver handshake recv: {err}");
        }
    };
    let reply_rid = handshake.routing_id().expect("receiver handshake rid").clone();
    assert_eq!(handshake.parts()[0].as_bytes(), b"PING");
    receiver
        .send(&reply_rid, Message::copy_from(b"PONG").expect("router pong"))
        .expect("receiver handshake reply");
    let handshake_reply = match sender.recv() {
        Ok(received) => received,
        Err(err) => {
            if common::handle_local_route_handshake_error(
                "ROUTER_ROUTER",
                &config.transport,
                "handshake_reply_recv",
                err,
            ) {
                return;
            }
            panic!("sender handshake recv: {err}");
        }
    };
    assert_eq!(handshake_reply.parts()[0].as_bytes(), b"PONG");

    let collector = common::MetricCollector::new();
    let stats = collector.shared();
    let deadline = std::time::Instant::now() + Duration::from_secs(config.duration_seconds);
    let mut seq: u64 = 0;
    let mut buf = vec![0u8; config.size.max(common::HEADER_SIZE)];
    while std::time::Instant::now() < deadline {
        common::encode_header(&mut buf, common::PHASE_ACTIVE, config.size as u32, seq);
        seq += 1;
        if sender
            .send(&target, Message::copy_from(&buf).expect("active msg"))
            .is_err()
        {
            continue;
        }
        if let Ok(received) = receiver.recv() {
            let data = common::message_payload(received.parts());
            common::handle_recv(data, config.size, &stats);
        }
    }

    let result = collector.finish();
    common::print_result("ROUTER_ROUTER", &config.transport, config.size, config.duration_seconds, &result);
}
