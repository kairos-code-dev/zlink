//! Single DEALER/DEALER throughput/latency benchmark.

mod common;

use std::time::Duration;
use zlink::*;

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let Some(bind_endpoint) = common::resolve_endpoint_or_emit_unsupported(
        "DEALER_DEALER",
        &config.transport,
        "dealer-dealer",
    ) else {
        return;
    };

    let ctx = common::perf_context();
    let receiver = ctx.dealer_socket().expect("receiver");
    let sender = ctx.dealer_socket().expect("sender");
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
    if matches!(config.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        common::setup_raw_tls_server(&receiver, &tls).expect("receiver tls");
        common::setup_raw_tls_client(&sender, &tls).expect("sender tls");
    }

    let mut receiver_mon = SocketMonitor::open(&receiver).expect("receiver monitor");
    let mut mon = SocketMonitor::open(&sender).expect("monitor");
    if let Err(err) = receiver.bind(&bind_endpoint) {
        if common::handle_transport_setup_error("DEALER_DEALER", &config.transport, "bind", err) {
            return;
        }
        panic!("bind: {err}");
    }
    let endpoint = receiver.last_endpoint().unwrap_or(bind_endpoint);
    if let Err(err) = sender.connect(&endpoint) {
        if common::handle_transport_setup_error("DEALER_DEALER", &config.transport, "connect", err)
        {
            return;
        }
        panic!("connect: {err}");
    }
    let ready_timeout = common::resolve_single_ready_timeout();
    common::wait_monitor_ready(&mut receiver_mon, ready_timeout, "dealer-dealer receiver");
    common::wait_monitor_ready(&mut mon, ready_timeout, "dealer-dealer sender");
    let collector = common::MetricCollector::new();
    let stats = collector.shared();
    let drain_receiver = |collect_active| {
        let mut saw_message = false;
        loop {
            match receiver.recv_with_flags(RecvFlags::DONT_WAIT) {
                Ok(Some(received)) => {
                    let data = common::message_payload(received.parts());
                    if collect_active {
                        common::handle_recv(data, config.size, &stats);
                    }
                    saw_message = true;
                }
                Ok(None) => break,
                Err(_) => break,
            }
        }
        saw_message
    };

    let active = Duration::from_secs(config.duration_seconds);
    let idle_drain = Duration::from_millis(common::resolve_single_idle_drain_ms());
    let active_deadline = std::time::Instant::now() + active;
    let hard_deadline = active_deadline + idle_drain + ready_timeout;
    let poller = Poller::new().expect("poller");
    poller.add_socket(&receiver, POLLIN).expect("poller add");
    let done = common::CompletionSignal::new();
    let sender_done = done.clone();
    let send_thread = std::thread::spawn(move || {
        common::send_loop(active_deadline, config.size, common::PHASE_ACTIVE, |msg| {
            match sender.try_send(msg) {
                Ok(sent) => sent,
                Err(err) if err.code() == SubmitResult::NotConnected => false,
                Err(err) => panic!("active send: {err}"),
            }
        });
        sender_done.signal_done();
    });
    common::run_single_recv_loop(
        &poller,
        active_deadline,
        hard_deadline,
        done,
        idle_drain,
        drain_receiver,
    );
    send_thread.join().expect("sender thread");

    let result = collector.finish();
    common::print_result(
        "DEALER_DEALER",
        &config.transport,
        config.size,
        config.duration_seconds,
        &result,
    );
}
