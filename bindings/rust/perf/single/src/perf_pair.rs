//! Single PAIR throughput/latency benchmark.

mod common;

use zlink::*;

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let Some(bind_endpoint) =
        common::resolve_endpoint_or_emit_unsupported("PAIR", &config.transport, "pair")
    else {
        return;
    };

    let ctx = common::perf_context();
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
    if matches!(config.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        common::setup_raw_tls_server(&receiver, &tls).expect("receiver tls");
        common::setup_raw_tls_client(&sender, &tls).expect("sender tls");
    }

    let mut receiver_mon = SocketMonitor::open(&receiver).expect("receiver monitor");
    let mut mon = SocketMonitor::open(&sender).expect("monitor");
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
    common::wait_monitor_ready(&mut receiver_mon, ready_timeout, "pair receiver");
    common::wait_monitor_ready(&mut mon, ready_timeout, "pair sender");
    let collector = common::MetricCollector::new();
    let stats = collector.shared();

    // PERF_SINGLE_TEST_POLICY § 1.4: sender signals phase end via wire-level
    // stop token; receiver loops on blocking `recv()` and exits when the
    // stop token arrives on the wire (multi/single shared idiom).
    let active = std::time::Duration::from_secs(config.duration_seconds);
    let active_deadline = std::time::Instant::now() + active;
    let send_thread = std::thread::spawn(move || {
        common::send_loop(active_deadline, config.size, common::PHASE_ACTIVE, |msg| {
            match sender.try_send(msg) {
                Ok(sent) => sent,
                Err(err) if err.code() == SubmitResult::NotConnected => false,
                Err(err) => panic!("active send: {err}"),
            }
        });
        common::send_stop_token(|msg| sender.send(msg).map_err(Into::into));
    });

    let mut received = zlink::Received::empty();
    loop {
        match receiver.recv(&mut received, zlink::RecvFlags::NONE) {
            Ok(true) => {
                let data = common::message_payload(received.parts());
                if common::is_stop_token(data) {
                    break;
                }
                common::handle_recv(data, config.size, &stats);
            }
            Ok(false) => continue,
            Err(err) => panic!("pair receiver recv failed: {err}"),
        }
    }
    send_thread.join().expect("sender thread");

    let result = collector.finish();
    common::print_result(
        "PAIR",
        &config.transport,
        config.size,
        config.duration_seconds,
        &result,
    );
}
