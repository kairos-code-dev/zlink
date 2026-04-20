//! Single SPOT throughput/latency benchmark.

mod common;

use std::sync::{
    atomic::{AtomicBool, AtomicU64, Ordering},
    Arc,
};
use std::thread;
use std::time::{Duration, Instant};

use zlink::*;

const SERVICE_NAME: &str = "perf-spot-svc";
const TOPIC: &str = "bench.topic";

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let Some(bind_endpoint) =
        common::resolve_endpoint_or_emit_unsupported("SPOT", &config.transport, "spot")
    else {
        return;
    };

    let ctx = Context::new().expect("context");
    let publisher_node = SpotNode::new(&ctx).expect("publisher node");
    let subscriber_node = SpotNode::new(&ctx).expect("subscriber node");
    if matches!(config.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        let pem = common::load_tls_pem(&tls);
        publisher_node
            .set_tls_server(&pem.cert, &pem.key, false)
            .expect("publisher tls");
        subscriber_node
            .set_tls_client(&pem.ca, "localhost", false)
            .expect("subscriber tls");
    }
    let publisher = publisher_node.create_spot().expect("publisher spot");
    let mut subscriber = subscriber_node.create_spot().expect("subscriber spot");
    subscriber
        .set_subscription(TOPIC)
        .expect("set subscription");

    if let Err(err) = publisher_node.bind(&bind_endpoint) {
        if common::handle_transport_setup_error("SPOT", &config.transport, "bind", err) {
            return;
        }
        panic!("bind: {err}");
    }
    let endpoint = publisher_node.last_endpoint().expect("endpoint");
    subscriber_node.connect_peer(&endpoint).expect("connect peer");

    let collector = common::MetricCollector::new();
    let stats = collector.shared();
    let ready_seen = Arc::new(AtomicBool::new(false));
    let active_collect = Arc::new(AtomicBool::new(false));
    let last_recv_ns = Arc::new(AtomicU64::new(0));

    let subscriber_ptr = (&subscriber as *const Spot) as usize;
    subscriber
        .on_dispatch_event({
            let ready_seen = Arc::clone(&ready_seen);
            let active_collect = Arc::clone(&active_collect);
            let last_recv_ns = Arc::clone(&last_recv_ns);
            move |event| {
                if event != SpotDispatchEvent::SubscribeReadable {
                    return;
                }

                let subscriber = unsafe { &*(subscriber_ptr as *const Spot) };
                loop {
                    match subscriber.subscribe_with_flags(RecvFlags::DONT_WAIT) {
                        Ok(received) => {
                            let data = common::message_payload(received.parts());
                            if !common::is_valid_message(data, config.size) {
                                continue;
                            }

                            last_recv_ns.store(common::now_ns(), Ordering::Release);
                            if active_collect.load(Ordering::Acquire) {
                                common::handle_recv(data, config.size, &stats);
                            } else {
                                ready_seen.store(true, Ordering::Release);
                            }
                        }
                        Err(err) if err.code() == RecvResult::NoData => break,
                        Err(err) => panic!("spot subscribe drain failed: {err}"),
                    }
                }
            }
        })
        .expect("on dispatch event");

    let probe_deadline = Instant::now() + common::resolve_single_ready_timeout();
    let mut probe = vec![0u8; config.size.max(common::HEADER_SIZE)];
    common::encode_header(&mut probe, common::PHASE_WARMUP, config.size as u32, 0);

    while Instant::now() < probe_deadline && !ready_seen.load(Ordering::Acquire) {
        match publisher.publish(
            SERVICE_NAME,
            TOPIC,
            Message::copy_from(&probe).expect("probe message"),
        ) {
            Ok(()) => {}
            Err(err)
                if matches!(
                    err.code(),
                    SubmitResult::NotConnected | SubmitResult::NotFound | SubmitResult::Backpressured
                ) =>
            {
                thread::sleep(Duration::from_millis(10));
                continue;
            }
            Err(err) => panic!("probe publish: {err}"),
        }
    }
    if !ready_seen.load(Ordering::Acquire) {
        panic!("single SPOT ready probe timed out");
    }

    thread::sleep(common::resolve_single_spot_ready_settle());

    active_collect.store(true, Ordering::Release);
    common::send_loop(
        Duration::from_secs(config.duration_seconds),
        config.size,
        common::PHASE_ACTIVE,
        |msg| {
            publisher
                .publish(SERVICE_NAME, TOPIC, msg)
                .expect("active publish");
        },
    );
    active_collect.store(false, Ordering::Release);

    let idle_drain = Duration::from_millis(common::resolve_single_idle_drain_ms());
    let mut idle_since = Instant::now();
    let mut last_seen = last_recv_ns.load(Ordering::Acquire);
    loop {
        let current = last_recv_ns.load(Ordering::Acquire);
        if current != last_seen {
            last_seen = current;
            idle_since = Instant::now();
        }
        if idle_since.elapsed() >= idle_drain {
            break;
        }
        thread::sleep(Duration::from_millis(10));
    }

    let result = collector.finish();
    common::print_result(
        "SPOT",
        &config.transport,
        config.size,
        config.duration_seconds,
        &result,
    );
}
