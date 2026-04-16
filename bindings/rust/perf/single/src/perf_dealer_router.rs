//! Single DEALER/ROUTER throughput/latency benchmark.

mod common;

use std::thread;
use std::time::{Duration, Instant};
use zlink::*;

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let bind_endpoint = config.endpoint("dealer-router");

    let ctx = Context::new().expect("context");
    let mut router = ctx.router_socket().expect("router");
    let dealer = ctx.dealer_socket().expect("dealer");
    let rid = RoutingId::from_bytes(b"perf-dealer");
    dealer.set_routing_id(&rid).expect("set rid");
    router
        .common_options()
        .set_recv_timeout(Duration::from_millis(1))
        .expect("recv timeout");

    if matches!(config.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        common::setup_raw_tls_server(&router, &tls).expect("router tls");
        common::setup_raw_tls_client(&dealer, &tls).expect("dealer tls");
    }

    let router_mon = SocketMonitor::open(&router).expect("router monitor");
    let mon = SocketMonitor::open(&dealer).expect("monitor");
    router.bind(&bind_endpoint).expect("bind");
    let endpoint = router.last_endpoint().unwrap_or(bind_endpoint);
    dealer.connect(&endpoint).expect("connect");
    common::wait_monitor_ready(&router_mon);
    common::wait_monitor_ready(&mon);

    let collector = common::MetricCollector::new();
    let stats = collector.shared();
    let sender_done = common::CompletionSignal::new();
    let receiver_done = sender_done.clone();

    let receiver_thread = thread::spawn(move || {
        let deadline = Instant::now() + Duration::from_secs(config.duration_seconds + 20);
        let mut idle_since: Option<Instant> = None;

        loop {
            if Instant::now() >= deadline {
                break;
            }

            let mut saw_message = false;
            loop {
                match router.recv() {
                    Ok(received) => {
                        let data = common::message_payload(received.parts());
                        common::handle_recv(data, config.size, &stats);
                        saw_message = true;
                    }
                    Err(_) => break,
                }
            }
            if saw_message {
                idle_since = None;
            }

            if receiver_done.is_done() {
                idle_since.get_or_insert_with(Instant::now);
                if idle_since
                    .map(|since| since.elapsed() >= Duration::from_millis(250))
                    .unwrap_or(false)
                {
                    break;
                }
            }
        }
    });

    let a = Duration::from_secs(config.duration_seconds);
    let sz = config.size;
    common::send_loop(
        a,
        sz,
        common::PHASE_ACTIVE,
        |msg| {
            let _ = dealer.send(msg);
        },
    );
    sender_done.signal_done();
    receiver_thread.join().expect("join");

    let result = collector.finish();
    common::print_result("DEALER_ROUTER", &config.transport, config.size, config.duration_seconds, &result);
}
