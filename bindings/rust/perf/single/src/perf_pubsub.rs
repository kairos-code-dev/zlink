//! Single PUB/SUB throughput/latency benchmark.

mod common;

use std::time::Duration;
use zlink::*;

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let Some(bind_endpoint) =
        common::resolve_endpoint_or_emit_unsupported("PUBSUB", &config.transport, "pubsub")
    else {
        return;
    };

    let ctx = Context::new().expect("context");
    let pub_sock = ctx.pub_socket().expect("pub");
    let sub_sock = ctx.sub_socket().expect("sub");
    pub_sock
        .common_options()
        .set_send_hwm(common::resolve_single_send_hwm())
        .expect("pub sndhwm");
    pub_sock
        .common_options()
        .set_recv_hwm(common::resolve_single_recv_hwm())
        .expect("pub rcvhwm");
    sub_sock
        .common_options()
        .set_send_hwm(common::resolve_single_send_hwm())
        .expect("sub sndhwm");
    sub_sock
        .common_options()
        .set_recv_hwm(common::resolve_single_recv_hwm())
        .expect("sub rcvhwm");
    pub_sock
        .common_options()
        .set_send_timeout(common::resolve_single_send_timeout())
        .expect("pub sndtimeo");

    if matches!(config.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        common::setup_raw_tls_server(&pub_sock, &tls).expect("pub tls");
        common::setup_raw_tls_client(&sub_sock, &tls).expect("sub tls");
    }

    let pub_mon = SocketMonitor::open(&pub_sock).expect("pub monitor");
    let mon = SocketMonitor::open(&sub_sock).expect("monitor");
    if let Err(err) = pub_sock.bind(&bind_endpoint) {
        if common::handle_transport_setup_error("PUBSUB", &config.transport, "bind", err) {
            return;
        }
        panic!("bind: {err}");
    }
    let endpoint = pub_sock.last_endpoint().unwrap_or(bind_endpoint);
    if let Err(err) = sub_sock.connect(&endpoint) {
        if common::handle_transport_setup_error("PUBSUB", &config.transport, "connect", err) {
            return;
        }
        panic!("connect: {err}");
    }
    sub_sock.set_subscription("").expect("subscribe");
    let ready_timeout = common::resolve_single_ready_timeout();
    common::wait_monitor_ready(&pub_mon, ready_timeout, "pubsub publisher");
    common::wait_monitor_ready(&mon, ready_timeout, "pubsub subscriber");
    common::wait_send_probe_ready("pubsub perf endpoint", config.size, ready_timeout, |msg| {
        pub_sock.publish("P", msg)
    }, || {
        let mut saw_probe = false;
        loop {
            match sub_sock.subscribe_with_flags(RecvFlags::DONT_WAIT) {
                Ok(topic_msg) => {
                    let data = common::message_payload(topic_msg.parts());
                    if common::is_valid_message(data, config.size) {
                        saw_probe = true;
                    }
                }
                Err(err) if err.code() == RecvResult::NoData => break,
                Err(_) => break,
            }
        }
        saw_probe
    });
    std::thread::sleep(common::resolve_single_pubsub_ready_settle());

    let collector = common::MetricCollector::new();
    let stats = collector.shared();
    let drain_sub = || {
        let mut saw_message = false;
        loop {
            match sub_sock.subscribe_with_flags(RecvFlags::DONT_WAIT) {
                Ok(topic_msg) => {
                    let data = common::message_payload(topic_msg.parts());
                    common::handle_recv(data, config.size, &stats);
                    saw_message = true;
                }
                Err(err) if err.code() == RecvResult::NoData => break,
                Err(_) => break,
            }
        }
        saw_message
    };

    let active = Duration::from_secs(config.duration_seconds);
    let idle_drain = Duration::from_millis(common::resolve_single_pubsub_idle_drain_ms());
    common::send_loop(active, config.size, common::PHASE_ACTIVE, |msg| {
        let _ = pub_sock.publish("P", msg);
        let _ = drain_sub();
    });
    let mut idle_since = std::time::Instant::now();
    while idle_since.elapsed() < idle_drain {
        if drain_sub() {
            idle_since = std::time::Instant::now();
        } else {
            std::thread::sleep(Duration::from_millis(1));
        }
    }

    let result = collector.finish();
    common::print_result("PUBSUB", &config.transport, config.size, config.duration_seconds, &result);
}
