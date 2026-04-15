//! Single SPOT throughput/latency benchmark.

mod common;

use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc,
};
use std::thread;
use std::time::{Duration, Instant};
use zlink::*;

const SERVICE_NAME: &str = "perf.spot";

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

fn attach_service_pair(
    ctx: &Context,
    node: &SpotNode,
    service_name: &str,
) -> (PubSocket, SubSocket) {
    let pub_sock = ctx.pub_socket().expect("pub socket");
    let sub_sock = ctx.sub_socket().expect("sub socket");
    let endpoint = format!("tcp://127.0.0.1:{}", reserve_tcp_port());
    pub_sock.bind(&endpoint).expect("pub bind");
    sub_sock.connect(&endpoint).expect("sub connect");
    node.attach_pubsub(service_name, &pub_sock, &sub_sock)
        .expect("attach_pubsub");
    (pub_sock, sub_sock)
}

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let topic = format!("perf.topic.{}", std::process::id());

    let ctx = Context::new().expect("context");
    let pub_node = SpotNode::new(&ctx).expect("publisher node");
    let sub_node = SpotNode::new(&ctx).expect("subscriber node");
    let publisher = pub_node.create_spot().expect("publisher spot");
    let mut subscriber = sub_node.create_spot().expect("subscriber spot");

    if matches!(config.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        pub_node
            .set_tls_server(&tls.cert, &tls.key, false)
            .expect("publisher tls");
        sub_node
            .set_tls_client(&tls.ca, "localhost", false)
            .expect("subscriber tls");
    }

    let _publisher_service = attach_service_pair(&ctx, &pub_node, SERVICE_NAME);
    let _subscriber_service = attach_service_pair(&ctx, &sub_node, SERVICE_NAME);

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
    let dispatch_pending = Arc::new(AtomicBool::new(false));
    let dispatch_trigger = dispatch_pending.clone();
    subscriber
        .on_dispatch_event(move |event| {
            if event == SpotDispatchEvent::SubscribeReadable {
                dispatch_trigger.store(true, Ordering::Release);
            }
        })
        .expect("on_dispatch_event failed");

    let receiver_thread = thread::spawn(move || {
        let mut idle_since: Option<Instant> = None;
        loop {
            let mut saw_message = false;
            if dispatch_pending.swap(false, Ordering::AcqRel) {
                loop {
                    match subscriber.subscribe_with_flags(RecvFlags::DONT_WAIT) {
                        Ok(topic_msg) => {
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
                        Err(_) => break,
                    }
                }
            } else {
                thread::yield_now();
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

    let mut probe_buf = vec![0u8; config.size.max(common::HEADER_SIZE)];
    common::encode_header(&mut probe_buf, common::PHASE_ACTIVE, config.size as u32, 0);
    let probe_deadline = Instant::now() + Duration::from_secs(10);
    while !ready.is_done() {
        if Instant::now() >= probe_deadline {
            panic!("spot local probe ready did not finish before timeout");
        }
        let probe = Message::copy_from(&probe_buf).expect("probe");
        publisher
            .publish(SERVICE_NAME, &topic, probe)
            .expect("probe publish");
        thread::yield_now();
    }

    thread::sleep(Duration::from_millis(settle_ms()));
    measurement_started.signal_done();

    let a = Duration::from_secs(config.duration_seconds);
    let sz = config.size;
    common::send_loop(a, sz, common::PHASE_ACTIVE, |msg| {
        let _ = publisher.publish(SERVICE_NAME, &topic, msg);
    });
    sender_done.signal_done();
    receiver_thread.join().expect("join");

    let result = collector.finish();
    common::print_result(
        "SPOT",
        &config.transport,
        config.size,
        config.duration_seconds,
        &result,
    );
}
