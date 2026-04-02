//! Single PAIR throughput/latency benchmark (callback-only).

mod common;

use std::sync::{Arc, Mutex, atomic::{AtomicBool, Ordering}};
use std::thread;
use std::time::{Duration, Instant};
use zlink::*;

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let endpoint = config.endpoint("pair");

    let ctx = Context::new().expect("context");
    let mut receiver = ctx.pair_socket().expect("receiver");
    let sender = ctx.pair_socket().expect("sender");

    receiver.bind(&endpoint).expect("bind");
    sender.connect(&endpoint).expect("connect");

    let mon = SocketMonitor::open(&sender, MONITOR_EVENT_ALL).expect("monitor");
    common::wait_monitor_ready(&mon);

    let stats = Arc::new(Mutex::new(common::LatencyStats::new()));
    let finished = Arc::new(AtomicBool::new(false));
    let (sc, fc) = (stats.clone(), finished.clone());

    receiver.on_receive(move |received| {
        common::handle_recv(received.parts()[0].data(), &sc, &fc);
    }).expect("on_receive");

    let w = Duration::from_secs(config.warmup_seconds);
    let a = Duration::from_secs(config.duration_seconds);
    let sz = config.size;

    let t = thread::spawn(move || {
        common::send_loop(w, a, sz,
            |msg| { let _ = sender.send(msg); },
            |msg| sender.try_send(msg),
        );
    });

    common::wait_finished(&finished, config.warmup_seconds, config.duration_seconds);
    t.join().expect("join");

    let result = stats.lock().unwrap().finish();
    common::print_result("PAIR", &config.transport, config.size, config.duration_seconds, &result);
}
