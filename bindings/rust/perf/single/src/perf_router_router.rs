//! Single ROUTER/ROUTER throughput/latency benchmark.

mod common;

use std::thread;
use std::time::{Duration, Instant};
use zlink::*;

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let bind_endpoint = config.endpoint("router-router");

    let ctx = Context::new().expect("context");
    let mut receiver = ctx.router_socket().expect("receiver");
    let sender = ctx.router_socket().expect("sender");

    let sender_rid = RoutingId::new(b"perf-rr-sender").expect("rid");
    sender.set_routing_id(&sender_rid).expect("set rid");
    let receiver_rid = RoutingId::new(b"perf-rr-receiver").expect("rid");
    receiver.set_routing_id(&receiver_rid).expect("set rid");
    sender
        .router_options()
        .set_connect_routing_id(&receiver_rid)
        .expect("connect rid");

    if matches!(config.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        common::setup_raw_tls_server(&receiver, &tls).expect("receiver tls");
        common::setup_raw_tls_client(&sender, &tls).expect("sender tls");
    }

    receiver.bind(&bind_endpoint).expect("bind");
    let endpoint = receiver.last_endpoint().unwrap_or(bind_endpoint);
    sender.connect(&endpoint).expect("connect");

    let mon = SocketMonitor::open(&sender).expect("monitor");
    common::wait_monitor_ready(&mon);

    let collector = common::MetricCollector::new();
    let stats = collector.shared();
    let sender_done = common::CompletionSignal::new();
    let receiver_done = sender_done.clone();
    let target = receiver_rid.clone();

    let receiver_thread = thread::spawn(move || {
        let poller = Poller::new().expect("poller");
        poller.add_socket(&receiver, 0, POLLIN).expect("poller add");
        let deadline = Instant::now() + Duration::from_secs(config.duration_seconds + 20);
        let mut idle_since: Option<Instant> = None;

        loop {
            if Instant::now() >= deadline {
                break;
            }

            match poller.wait(100) {
                Ok(Some(_)) => {
                    let mut saw_message = false;
                    loop {
                        match receiver.try_recv() {
                            Ok(Some(received)) => {
                                let data = common::message_payload(received.parts());
                                common::handle_recv(data, config.size, &stats);
                                saw_message = true;
                            }
                            Ok(None) => break,
                            Err(_) => break,
                        }
                    }
                    if saw_message {
                        idle_since = None;
                    }
                }
                Ok(None) => {}
                Err(_) => break,
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
            let _ = sender.send(&target, msg);
        },
    );
    sender_done.signal_done();
    receiver_thread.join().expect("join");

    let result = collector.finish();
    common::print_result("ROUTER_ROUTER", &config.transport, config.size, config.duration_seconds, &result);
}
