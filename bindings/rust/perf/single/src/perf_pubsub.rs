//! Single PUB/SUB throughput/latency benchmark (callback-only).

mod common;

use std::thread;
use std::time::Duration;
use zlink::*;

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let bind_endpoint = config.endpoint("pubsub");

    let ctx = Context::new().expect("context");
    let pub_sock = ctx.pub_socket().expect("pub");
    let mut sub_sock = ctx.sub_socket().expect("sub");

    pub_sock.bind(&bind_endpoint).expect("bind");
    let endpoint = pub_sock.last_endpoint().unwrap_or(bind_endpoint);
    sub_sock.connect(&endpoint).expect("connect");
    sub_sock.set_subscription("").expect("subscribe");

    let mon = SocketMonitor::open(&sub_sock).expect("monitor");
    common::wait_monitor_ready(&mon);

    let collector = common::MetricCollector::new();
    let stats = collector.shared();
    let finished = common::CompletionSignal::new();
    let sender_done = finished.clone();
    let sc = stats.clone();
    let finished_cb = finished.clone();

    sub_sock.on_subscribe(move |topic_msg| {
        let data = common::callback_payload(topic_msg.parts());
        if common::decode_phase(data) == common::PHASE_STOP {
            finished_cb.signal_done();
            return;
        }
        common::handle_recv(data, &sc);
    }).expect("on_subscribe");

    let a = Duration::from_secs(config.duration_seconds);
    let sz = config.size;

    let t = thread::spawn(move || {
        let _guard = common::CompletionGuard::new(sender_done);
        common::send_loop(a, sz,
            |msg| { let _ = pub_sock.publish("P", msg); },
            |msg| pub_sock.try_publish("P", msg),
        );
        for _ in 0..16 {
            let mut stop_buf = vec![0u8; sz.max(common::HEADER_SIZE)];
            common::encode_header(&mut stop_buf, common::PHASE_STOP, sz as u32, 0);
            let stop = Message::from_bytes(&stop_buf).expect("stop msg");
            let _ = pub_sock.publish("P", stop);
        }
    });

    common::wait_finished(&finished, config.duration_seconds);
    t.join().expect("join");

    let result = collector.finish();
    common::print_result("PUBSUB", &config.transport, config.size, config.duration_seconds, &result);
}
