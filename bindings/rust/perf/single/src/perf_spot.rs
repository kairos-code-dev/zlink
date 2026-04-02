//! Single SPOT throughput/latency benchmark (callback-only).

mod common;

use std::sync::{Arc, Mutex, atomic::{AtomicBool, Ordering}};
use std::thread;
use std::time::{Duration, Instant};
use zlink::*;

fn main() {
    let config = common::PerfConfig::from_env_and_args();

    let ctx = Context::new().expect("context");
    let pub_sock = ctx.pub_socket().expect("pub");
    let mut sub_sock = ctx.sub_socket().expect("sub");

    let endpoint = "inproc://perf-spot-pubsub";
    pub_sock.bind(endpoint).expect("bind");
    sub_sock.connect(endpoint).expect("connect");
    sub_sock.set_subscription("").expect("subscribe");

    let mon = SocketMonitor::open(&pub_sock, MONITOR_EVENT_ALL).expect("monitor");
    common::wait_monitor_ready(&mon);
    thread::sleep(Duration::from_millis(100));

    let stats = Arc::new(Mutex::new(common::LatencyStats::new()));
    let finished = Arc::new(AtomicBool::new(false));
    let (sc, fc) = (stats.clone(), finished.clone());

    sub_sock.on_subscribe(move |topic_msg| {
        common::handle_recv(topic_msg.parts()[0].data(), &sc, &fc);
    }).expect("on_subscribe");

    let w = Duration::from_secs(config.warmup_seconds);
    let a = Duration::from_secs(config.duration_seconds);
    let sz = config.size;

    let t = thread::spawn(move || {
        common::send_loop(w, a, sz,
            |msg| { let _ = pub_sock.publish("S", msg); },
            |msg| pub_sock.try_publish("S", msg),
        );
    });

    common::wait_finished(&finished, config.warmup_seconds, config.duration_seconds);
    t.join().expect("join");

    let result = stats.lock().unwrap().finish();
    common::print_result("SPOT", &config.transport, config.size, config.duration_seconds, &result);
}
