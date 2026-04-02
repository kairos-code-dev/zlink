//! Single ROUTER/ROUTER throughput/latency benchmark (callback-only).

mod common;

use std::sync::{Arc, Mutex, atomic::{AtomicBool, Ordering}};
use std::thread;
use std::time::{Duration, Instant};
use zlink::*;

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let endpoint = config.endpoint("router-router");

    let ctx = Context::new().expect("context");
    let mut receiver = ctx.router_socket().expect("receiver");
    let sender = ctx.router_socket().expect("sender");

    let sender_rid = RoutingId::new(b"perf-rr-sender").expect("rid");
    sender.set_routing_id(&sender_rid).expect("set rid");
    let receiver_rid = RoutingId::new(b"perf-rr-receiver").expect("rid");
    receiver.set_routing_id(&receiver_rid).expect("set rid");
    sender.set_connect_routing_id(&receiver_rid).expect("connect rid");

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
    let target = receiver_rid.clone();

    let t = thread::spawn(move || {
        common::send_loop(w, a, sz,
            |msg| { let _ = sender.send(&target, msg); },
            |msg| sender.try_send(&target, msg),
        );
    });

    common::wait_finished(&finished, config.warmup_seconds, config.duration_seconds);
    t.join().expect("join");

    let result = stats.lock().unwrap().finish();
    common::print_result("ROUTER_ROUTER", &config.transport, config.size, config.duration_seconds, &result);
}
