//! Single PUB/SUB throughput/latency benchmark (callback-only).

mod common;

use std::sync::{Arc, Mutex, atomic::{AtomicBool, Ordering}};
use std::thread;
use std::time::{Duration, Instant};
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
            |msg| { let _ = pub_sock.publish("P", msg); },
            |msg| pub_sock.try_publish("P", msg),
        );
    });

    common::wait_finished(&finished, config.warmup_seconds, config.duration_seconds);
    t.join().expect("join");

    let result = stats.lock().unwrap().finish();
    common::print_result("PUBSUB", &config.transport, config.size, config.duration_seconds, &result);
}
