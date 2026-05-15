//! Single SPOT throughput/latency benchmark.

mod common;

use std::thread;
use std::time::{Duration, Instant};

use zlink::*;

const TOPIC: &str = "bench";

fn drain_spot_readable(
    subscriber: &Spot,
    config: &common::PerfConfig,
    stats: Option<&std::sync::Arc<std::sync::Mutex<common::LatencyStats>>>,
    collect_active: bool,
    active_deadline: Option<Instant>,
) -> bool {
    let mut processed = false;
    loop {
        let mut received = TopicMessage::empty();
        match subscriber.subscribe(&mut received, RecvFlags::DONT_WAIT) {
            Ok(true) => {
                {
                    let data = common::message_payload(received.parts());
                    if collect_active {
                        if let Some(stats) = stats {
                            if let Some(active_deadline) = active_deadline {
                                common::handle_recv(data, config.size, stats, active_deadline);
                            }
                        }
                    }
                }
                processed = true;
            }
            Ok(false) => break,
            Err(err) => panic!("spot subscribe drain failed: {err}"),
        }
    }
    processed
}

fn wait_for_spot_ready(publisher: &Spot, subscriber: &Spot, config: &common::PerfConfig) {
    let deadline = Instant::now() + common::resolve_single_ready_timeout();
    let mut probe = vec![0u8; common::HEADER_SIZE];
    common::encode_header(&mut probe, common::PHASE_WARMUP, config.size as u32, 0);
    while Instant::now() < deadline {
        match publisher
            .publish(TOPIC)
            .message(Message::copy_from(&probe).expect("probe message"))
            .submit()
        {
            Ok(_) => {}
            Err(err)
                if matches!(
                    err.code(),
                    SubmitResult::NotConnected
                        | SubmitResult::NotFound
                        | SubmitResult::Backpressured
                ) => {}
            Err(err) => panic!("probe publish: {err}"),
        }
        if drain_spot_readable(subscriber, config, None, false, None) {
            return;
        }
        thread::sleep(Duration::from_millis(10));
    }
    panic!("single SPOT ready probe timed out");
}

fn spot_send_gap() -> Duration {
    let micros = std::env::var("PERF_SINGLE_SPOT_SEND_GAP_US")
        .ok()
        .and_then(|value| value.parse::<u64>().ok())
        .unwrap_or(0);
    Duration::from_micros(micros)
}

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let Some(publisher_endpoint) =
        common::resolve_endpoint_or_emit_unsupported("SPOT", &config.transport, "spot-pub")
    else {
        return;
    };

    let ctx = common::perf_context();
    let publisher_node = SpotNode::new(&ctx).expect("publisher node");
    let subscriber_node = SpotNode::new(&ctx).expect("subscriber node");
    common::apply_single_spot_node_admission(&publisher_node);
    common::apply_single_spot_node_admission(&subscriber_node);

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

    publisher_node
        .set_routing_id(&RoutingId::from_bytes(b"z-rust-perf-spot-publisher"))
        .expect("publisher node rid");
    subscriber_node
        .set_routing_id(&RoutingId::from_bytes(b"a-rust-perf-spot-subscriber"))
        .expect("subscriber node rid");

    let publisher = publisher_node.create_spot().expect("publisher spot");
    let stop_publisher = subscriber_node.create_spot().expect("stop publisher spot");
    let subscriber = subscriber_node.create_spot().expect("subscriber spot");
    publisher
        .set_routing_id(&RoutingId::from_bytes(b"z-rust-perf-spot-publisher-spot"))
        .expect("publisher spot rid");
    stop_publisher
        .set_routing_id(&RoutingId::from_bytes(b"m-rust-perf-spot-stop-spot"))
        .expect("stop publisher spot rid");
    subscriber
        .set_routing_id(&RoutingId::from_bytes(b"a-rust-perf-spot-subscriber-spot"))
        .expect("subscriber spot rid");

    subscriber
        .set_subscription(TOPIC)
        .expect("set subscription");
    publisher_node
        .bind(&publisher_endpoint)
        .expect("publisher bind");
    subscriber_node
        .connect_peer(&publisher_endpoint)
        .expect("subscriber connect peer");
    common::wait_spot_peer_connected(
        &subscriber_node,
        common::resolve_single_ready_timeout(),
        "single SPOT subscriber",
    );

    wait_for_spot_ready(&publisher, &subscriber, &config);
    thread::sleep(common::resolve_single_spot_ready_settle());

    let collector = common::MetricCollector::new();
    let stats = collector.shared();
    let active_deadline = Instant::now() + Duration::from_secs(config.duration_seconds);
    // PERF_SINGLE_TEST_POLICY § 1.4: sender publishes a wire-level stop token
    // at phase end; the receiver drains with DONT_WAIT and exits only when the
    // stop token arrives, matching the C SPOT single runner.
    let publisher_thread = thread::spawn({
        let send_gap = spot_send_gap();
        move || {
            let mut seq: u64 = 0;
            let mut payload = vec![0u8; config.size.max(common::HEADER_SIZE)];
            while Instant::now() < active_deadline {
                common::encode_header(&mut payload, common::PHASE_ACTIVE, config.size as u32, seq);
                match publisher
                    .publish(TOPIC)
                    .message(Message::copy_from(&payload).expect("active message"))
                    .flags(SendFlags::DONT_WAIT)
                    .submit()
                {
                    Ok(_) => {
                        seq += 1;
                    }
                    Err(err)
                        if matches!(
                            err.code(),
                            SubmitResult::NotConnected
                                | SubmitResult::NotFound
                                | SubmitResult::Backpressured
                        ) => {}
                    Err(err) => panic!("active publish: {err}"),
                }
                thread::sleep(send_gap);
            }

            common::send_stop_token(|msg| {
                stop_publisher
                    .publish(TOPIC)
                    .message(msg)
                    .submit()
                    .map(|_| ())
                    .map_err(Into::into)
            });
        }
    });

    let mut received = TopicMessage::empty();
    loop {
        match subscriber.subscribe(&mut received, RecvFlags::DONT_WAIT) {
            Ok(true) => {
                let data = common::message_payload(received.parts());
                if common::is_stop_token(data) {
                    break;
                }
                common::handle_recv(data, config.size, &stats, active_deadline);
            }
            Ok(false) => std::thread::yield_now(),
            Err(err) => panic!("spot subscriber recv failed: {err}"),
        }
    }
    publisher_thread.join().expect("publisher thread");

    let result = collector.finish();
    common::print_result(
        "SPOT",
        &config.transport,
        config.size,
        config.duration_seconds,
        &result,
    );
}
