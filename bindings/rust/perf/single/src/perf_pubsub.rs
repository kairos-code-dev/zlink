//! Single PUB/SUB throughput/latency benchmark (callback-only).

mod common;

use std::thread;
use std::time::Duration;
use zlink::*;

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let endpoint = config.endpoint("pubsub");

    let ctx = Context::new().expect("context");
    let pub_sock = ctx.pub_socket().expect("pub");
    let mut sub_sock = ctx.sub_socket().expect("sub");

    pub_sock.bind(&endpoint).expect("bind");
    sub_sock.connect(&endpoint).expect("connect");
    sub_sock.set_subscription("").expect("subscribe");

    let mon = SocketMonitor::open(&pub_sock, MONITOR_EVENT_ALL).expect("monitor");
    common::wait_monitor_ready(&mon);
    thread::sleep(Duration::from_millis(100)); // subscription propagation

    let collector = common::MetricCollector::new();
    let stats = collector.shared();
    let finished = common::CompletionSignal::new();
    let sender_done = finished.clone();
    let sc = stats.clone();

    sub_sock.on_subscribe(move |topic_msg| {
        common::handle_recv(topic_msg.parts()[0].data(), &sc);
    }).expect("on_subscribe");

    let w = Duration::from_secs(config.warmup_seconds);
    let a = Duration::from_secs(config.duration_seconds);
    let sz = config.size;

    let t = thread::spawn(move || {
        let _guard = common::CompletionGuard::new(sender_done);
        common::send_loop(w, a, sz,
            |msg| { let _ = pub_sock.publish("P", msg); },
            |msg| pub_sock.try_publish("P", msg),
        );
    });

    common::wait_finished(&finished, config.warmup_seconds, config.duration_seconds);
    t.join().expect("join");

    let result = collector.finish();
    common::print_result("PUBSUB", &config.transport, config.size, config.duration_seconds, &result);
}
