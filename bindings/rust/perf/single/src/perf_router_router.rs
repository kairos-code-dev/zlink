//! Single ROUTER/ROUTER throughput/latency benchmark (callback-only).

mod common;

use std::thread;
use std::time::Duration;
use zlink::*;

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let bind_endpoint = config.endpoint("router-router");

    let ctx = Context::new().expect("context");
    let mut receiver = ctx.router_socket().expect("receiver");
    let sender = ctx.router_socket().expect("sender");

    let sender_rid = RoutingId::new(b"perf-rr-sender").expect("rid");
    sender.set_routing_id(&sender_rid).expect("set rid");
    let receiver_rid = RoutingId::new(b"perf-rr-receiver").expect("rid");
    receiver.set_routing_id(&receiver_rid).expect("set rid");
    sender
        .router_options()
        .set_connect_routing_id(&receiver_rid)
        .expect("connect rid");

    receiver.bind(&bind_endpoint).expect("bind");
    let endpoint = receiver.last_endpoint().unwrap_or(bind_endpoint);
    sender.connect(&endpoint).expect("connect");

    let mon = SocketMonitor::open(&sender).expect("monitor");
    common::wait_monitor_ready(&mon);

    let collector = common::MetricCollector::new();
    let stats = collector.shared();
    let finished = common::CompletionSignal::new();
    let sender_done = finished.clone();
    let sc = stats.clone();

    receiver.on_receive(move |received| {
        common::handle_recv(common::callback_payload(received.parts()), &sc);
    }).expect("on_receive");

    let a = Duration::from_secs(config.duration_seconds);
    let sz = config.size;
    let target = receiver_rid.clone();

    let t = thread::spawn(move || {
        let _guard = common::CompletionGuard::new(sender_done);
        common::send_loop(a, sz,
            |msg| { let _ = sender.send(&target, msg); },
            |msg| sender.try_send(&target, msg),
        );
    });

    common::wait_finished(&finished, config.duration_seconds);
    t.join().expect("join");

    let result = collector.finish();
    common::print_result("ROUTER_ROUTER", &config.transport, config.size, config.duration_seconds, &result);
}
