//! Single PUB/SUB throughput/latency benchmark.

mod common;

use std::thread;
use std::time::{Duration, Instant};
use zlink::*;

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let bind_endpoint = config.endpoint("pubsub");

    let ctx = Context::new().expect("context");
    let pub_sock = ctx.pub_socket().expect("pub");
    let mut sub_sock = ctx.sub_socket().expect("sub");

    if matches!(config.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        common::setup_raw_tls_server(&pub_sock, &tls).expect("pub tls");
        common::setup_raw_tls_client(&sub_sock, &tls).expect("sub tls");
    }

    pub_sock.bind(&bind_endpoint).expect("bind");
    let endpoint = pub_sock.last_endpoint().unwrap_or(bind_endpoint);
    sub_sock.connect(&endpoint).expect("connect");
    sub_sock.set_subscription("").expect("subscribe");

    let mon = SocketMonitor::open(&sub_sock).expect("monitor");
    common::wait_monitor_ready(&mon);

    let collector = common::MetricCollector::new();
    let stats = collector.shared();
    let sender_done = common::CompletionSignal::new();
    let receiver_done = sender_done.clone();

    let receiver_thread = thread::spawn(move || {
        let poller = Poller::new().expect("poller");
        poller.add_socket(&sub_sock, 0, POLLIN).expect("poller add");
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
                        match sub_sock.try_subscribe() {
                            Ok(Some(topic_msg)) => {
                                let data = common::message_payload(topic_msg.parts());
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
            let _ = pub_sock.publish("P", msg);
        },
    );
    sender_done.signal_done();
    receiver_thread.join().expect("join");

    let result = collector.finish();
    common::print_result("PUBSUB", &config.transport, config.size, config.duration_seconds, &result);
}
