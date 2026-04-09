//! Single SPOT throughput/latency benchmark.

mod common;

use std::thread;
use std::time::{Duration, Instant};
use zlink::*;

fn reserve_tcp_port() -> u16 {
    let listener = std::net::TcpListener::bind("127.0.0.1:0").expect("reserve port");
    let port = listener.local_addr().expect("local_addr").port();
    drop(listener);
    port
}

fn settle_ms() -> u64 {
    std::env::var("PERF_SINGLE_SPOT_READY_SETTLE_MS")
        .ok()
        .and_then(|value| value.parse().ok())
        .unwrap_or(1000)
}

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let topic = format!("perf.topic.{}", std::process::id());

    let ctx = Context::new().expect("context");
    let pub_node = SpotNode::new(&ctx).expect("publisher node");
    let sub_node = SpotNode::new(&ctx).expect("subscriber node");
    let publisher = Spot::new(&pub_node).expect("publisher spot");
    let subscriber = Spot::new(&sub_node).expect("subscriber spot");

    if matches!(config.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        pub_node
            .set_tls_server(&tls.cert, &tls.key, false)
            .expect("publisher tls");
        sub_node
            .set_tls_client(&tls.ca, "localhost", false)
            .expect("subscriber tls");
    }

    let bind_endpoint = match config.transport.as_str() {
        "inproc" => "inproc://perf-spot-node".to_string(),
        "tcp" => format!("tcp://127.0.0.1:{}", reserve_tcp_port()),
        "tls" => format!("tls://127.0.0.1:{}", reserve_tcp_port()),
        "ws" => format!("ws://127.0.0.1:{}", reserve_tcp_port()),
        "wss" => format!("wss://127.0.0.1:{}", reserve_tcp_port()),
        _ => format!("tcp://127.0.0.1:{}", reserve_tcp_port()),
    };
    pub_node.bind(&bind_endpoint).expect("bind");
    let endpoint = pub_node.last_endpoint().unwrap_or(bind_endpoint);
    sub_node.connect_peer(&endpoint).expect("connect_peer");

    let collector = common::MetricCollector::new();
    let stats = collector.shared();
    let ready = common::CompletionSignal::new();
    let ready_seen = ready.clone();
    let measurement_started = common::CompletionSignal::new();
    let measurement_gate = measurement_started.clone();
    let sender_done = common::CompletionSignal::new();
    let receiver_done = sender_done.clone();

    subscriber.set_subscription(&topic).expect("subscribe");
    let receiver_thread = thread::spawn(move || {
        let mut idle_since: Option<Instant> = None;
        loop {
            let mut saw_message = false;
            loop {
                match subscriber.try_subscribe() {
                    Ok(Some(topic_msg)) => {
                        let data = common::message_payload(topic_msg.parts());
                        let phase = common::decode_phase(data);
                        saw_message = true;
                        if phase == common::PHASE_ACTIVE {
                            if !ready_seen.is_done() {
                                ready_seen.signal_done();
                                continue;
                            }
                            if !measurement_gate.is_done() {
                                continue;
                            }
                            common::handle_recv(data, config.size, &stats);
                        }
                    }
                    Ok(None) => break,
                    Err(_) => break,
                }
            }

            if saw_message {
                idle_since = None;
            } else {
                thread::yield_now();
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

    let mut probe_buf = vec![0u8; config.size.max(common::HEADER_SIZE)];
    common::encode_header(&mut probe_buf, common::PHASE_ACTIVE, config.size as u32, 0);
    let probe_deadline = Instant::now() + Duration::from_secs(10);
    while !ready.is_done() {
        if Instant::now() >= probe_deadline {
            panic!("spot local probe ready did not finish before timeout");
        }
        let probe = Message::from_bytes(&probe_buf).expect("probe");
        publisher.publish(&topic, probe).expect("probe publish");
        thread::yield_now();
    }

    thread::sleep(Duration::from_millis(settle_ms()));
    measurement_started.signal_done();

    let a = Duration::from_secs(config.duration_seconds);
    let sz = config.size;
    common::send_loop(
        a,
        sz,
        common::PHASE_ACTIVE,
        |msg| {
            let _ = publisher.publish(&topic, msg);
        },
    );
    sender_done.signal_done();
    receiver_thread.join().expect("join");

    let result = collector.finish();
    common::print_result("SPOT", &config.transport, config.size, config.duration_seconds, &result);
}
