//! Single SPOT throughput/latency benchmark (callback-only).

mod common;

use std::thread;
use std::time::Duration;
use zlink::*;

fn reserve_tcp_port() -> u16 {
    let listener = std::net::TcpListener::bind("127.0.0.1:0").expect("reserve port");
    let port = listener.local_addr().expect("local_addr").port();
    drop(listener);
    port
}

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let topic = format!("perf.topic.{}", std::process::id());

    let ctx = Context::new().expect("context");
    let pub_node = SpotNode::new(&ctx).expect("publisher node");
    let sub_node = SpotNode::new(&ctx).expect("subscriber node");
    let publisher = Spot::new(&pub_node).expect("publisher spot");
    let mut subscriber = Spot::new(&sub_node).expect("subscriber spot");

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
    let finished = common::CompletionSignal::new();
    let ready = common::CompletionSignal::new();
    let sender_done = finished.clone();
    let sc = stats.clone();
    let ready_cb = ready.clone();
    let finished_cb = finished.clone();

    subscriber.on_subscribe(move |topic_msg| {
        let data = common::callback_payload(topic_msg.parts());
        let phase = common::decode_phase(data);
        if phase == common::PHASE_PROBE {
            ready_cb.signal_done();
            return;
        }
        if phase == common::PHASE_STOP {
            finished_cb.signal_done();
            return;
        }
        common::handle_recv(data, &sc);
    }).expect("on_subscribe");
    subscriber.set_subscription(&topic).expect("subscribe");

    let mut probe_buf = vec![0u8; config.size.max(common::HEADER_SIZE)];
    common::encode_header(&mut probe_buf, common::PHASE_PROBE, config.size as u32, 0);
    let probe_deadline = std::time::Instant::now() + Duration::from_secs(10);
    while !ready.is_done() {
        if std::time::Instant::now() >= probe_deadline {
            panic!("spot local probe ready did not finish before timeout");
        }
        let probe = Message::from_bytes(&probe_buf).expect("probe");
        publisher.publish(&topic, probe).expect("probe publish");
        std::thread::yield_now();
    }

    let a = Duration::from_secs(config.duration_seconds);
    let sz = config.size;
    let send_topic = topic.clone();

    let t = thread::spawn(move || {
        let _guard = common::CompletionGuard::new(sender_done);
        common::send_loop(a, sz,
            |msg| { let _ = publisher.publish(&send_topic, msg); },
            |msg| publisher.try_publish(&send_topic, msg),
        );
        for _ in 0..16 {
            let mut stop_buf = vec![0u8; sz.max(common::HEADER_SIZE)];
            common::encode_header(&mut stop_buf, common::PHASE_STOP, sz as u32, 0);
            let stop = Message::from_bytes(&stop_buf).expect("stop msg");
            let _ = publisher.publish(&send_topic, stop);
        }
    });

    common::wait_finished(&finished, config.duration_seconds);
    t.join().expect("join");

    let result = collector.finish();
    common::print_result("SPOT", &config.transport, config.size, config.duration_seconds, &result);
}
