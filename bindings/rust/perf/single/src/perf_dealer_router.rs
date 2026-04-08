//! Single DEALER/ROUTER throughput/latency benchmark (callback-only).

mod common;

use std::thread;
use std::time::Duration;
use zlink::*;

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let bind_endpoint = config.endpoint("dealer-router");

    let ctx = Context::new().expect("context");
    let mut router = ctx.router_socket().expect("router");
    let dealer = ctx.dealer_socket().expect("dealer");
    let rid = RoutingId::new(b"perf-dealer").expect("rid");
    dealer.set_routing_id(&rid).expect("set rid");

    router.bind(&bind_endpoint).expect("bind");
    let endpoint = router.last_endpoint().unwrap_or(bind_endpoint);
    dealer.connect(&endpoint).expect("connect");

    let mon = SocketMonitor::open(&dealer, MONITOR_EVENT_ALL).expect("monitor");
    common::wait_monitor_ready(&mon);

    let collector = common::MetricCollector::new();
    let stats = collector.shared();
    let finished = common::CompletionSignal::new();
    let sender_done = finished.clone();
    let sc = stats.clone();

    router.on_receive(move |received| {
        common::handle_recv(common::callback_payload(received.parts()), &sc);
    }).expect("on_receive");

    let a = Duration::from_secs(config.duration_seconds);
    let sz = config.size;

    let t = thread::spawn(move || {
        let _guard = common::CompletionGuard::new(sender_done);
        common::send_loop(a, sz,
            |msg| { let _ = dealer.send(msg); },
            |msg| dealer.try_send(msg),
        );
    });

    common::wait_finished(&finished, config.duration_seconds);
    t.join().expect("join");

    let result = collector.finish();
    common::print_result("DEALER_ROUTER", &config.transport, config.size, config.duration_seconds, &result);
}
