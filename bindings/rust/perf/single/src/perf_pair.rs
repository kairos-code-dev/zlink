//! Single PAIR throughput/latency benchmark.

mod common;

use std::time::Duration;
use zlink::*;

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let Some(bind_endpoint) =
        common::resolve_endpoint_or_emit_unsupported("PAIR", &config.transport, "pair")
    else {
        return;
    };

    let ctx = Context::new().expect("context");
    let receiver = ctx.pair_socket().expect("receiver");
    let sender = ctx.pair_socket().expect("sender");
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

    if matches!(config.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        common::setup_raw_tls_server(&receiver, &tls).expect("receiver tls");
        common::setup_raw_tls_client(&sender, &tls).expect("sender tls");
    }

    let receiver_mon = SocketMonitor::open(&receiver).expect("receiver monitor");
    let mon = SocketMonitor::open(&sender).expect("monitor");
    if let Err(err) = receiver.bind(&bind_endpoint) {
        if common::handle_transport_setup_error("PAIR", &config.transport, "bind", err) {
            return;
        }
        panic!("bind: {err}");
    }
    let endpoint = receiver.last_endpoint().unwrap_or(bind_endpoint);
    if let Err(err) = sender.connect(&endpoint) {
        if common::handle_transport_setup_error("PAIR", &config.transport, "connect", err) {
            return;
        }
        panic!("connect: {err}");
    }
    let ready_timeout = common::resolve_single_ready_timeout();
    common::wait_monitor_ready(&receiver_mon, ready_timeout, "pair receiver");
    common::wait_monitor_ready(&mon, ready_timeout, "pair sender");
    common::wait_send_probe_ready("pair perf endpoint", config.size, ready_timeout, |msg| {
        sender.send(msg)
    }, || {
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
    });

    let collector = common::MetricCollector::new();
    let stats = collector.shared();
    let drain_receiver = || {
        let mut saw_message = false;
        loop {
            match receiver.recv_with_flags(RecvFlags::DONT_WAIT) {
                Ok(received) => {
                    let data = common::message_payload(received.parts());
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
    let idle_drain = Duration::from_millis(common::resolve_single_idle_drain_ms());
    common::send_loop(active, config.size, common::PHASE_ACTIVE, |msg| {
        let _ = sender.send(msg);
        let _ = drain_receiver();
    });
    let mut idle_since = std::time::Instant::now();
    while idle_since.elapsed() < idle_drain {
        if drain_receiver() {
            idle_since = std::time::Instant::now();
        } else {
            std::thread::sleep(Duration::from_millis(1));
        }
    }

    let result = collector.finish();
    common::print_result("PAIR", &config.transport, config.size, config.duration_seconds, &result);
}
