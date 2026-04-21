//! Single SPOT throughput/latency benchmark.

mod common;

use std::sync::mpsc;
use std::sync::{
    Arc,
    atomic::{AtomicBool, Ordering},
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

    let ctx = common::perf_context();
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
    subscriber_node
        .connect_peer(&endpoint)
        .expect("connect peer");

    let collector = common::MetricCollector::new();
    let stats = collector.shared();
    let active_collect = Arc::new(AtomicBool::new(false));
    let (ready_tx, ready_rx) = mpsc::channel::<()>();
    let (activity_tx, activity_rx) = mpsc::channel::<()>();

    let subscriber_ptr = (&subscriber as *const Spot) as usize;
    subscriber
        .on_dispatch_event({
            let active_collect = Arc::clone(&active_collect);
            let ready_tx = ready_tx.clone();
            let activity_tx = activity_tx.clone();
            move |info| {
                if info.event != SpotDispatchEvent::SubscribeReadable {
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

                            let _ = activity_tx.send(());
                            if active_collect.load(Ordering::Acquire) {
                                common::handle_recv(data, config.size, &stats);
                            } else {
                                let _ = ready_tx.send(());
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

    let mut ready_received = false;
    while Instant::now() < probe_deadline {
        match publisher.publish(
            SERVICE_NAME,
            TOPIC,
            Message::copy_from(&probe).expect("probe message"),
        ) {
            Ok(()) => {}
            Err(err)
                if matches!(
                    err.code(),
                    SubmitResult::NotConnected
                        | SubmitResult::NotFound
                        | SubmitResult::Backpressured
                ) =>
            {
                if let Ok(()) = ready_rx.recv_timeout(Duration::from_millis(10)) {
                    ready_received = true;
                    break;
                }
            }
            Err(err) => panic!("probe publish: {err}"),
        }
        if let Ok(()) = ready_rx.recv_timeout(Duration::from_millis(10)) {
            ready_received = true;
            break;
        }
    }
    if !ready_received {
        panic!("single SPOT ready probe timed out");
    }

    thread::sleep(common::resolve_single_spot_ready_settle());

    active_collect.store(true, Ordering::Release);
    let active_deadline = Instant::now() + Duration::from_secs(config.duration_seconds);
    common::send_loop(active_deadline, config.size, common::PHASE_ACTIVE, |msg| {
        publisher
            .publish(SERVICE_NAME, TOPIC, msg)
            .expect("active publish");
    });
    active_collect.store(false, Ordering::Release);

    let idle_drain = Duration::from_millis(common::resolve_single_idle_drain_ms());
    loop {
        match activity_rx.recv_timeout(idle_drain) {
            Ok(()) => {}
            Err(mpsc::RecvTimeoutError::Timeout) => break,
            Err(mpsc::RecvTimeoutError::Disconnected) => {
                panic!("single SPOT activity channel disconnected");
            }
        }
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
