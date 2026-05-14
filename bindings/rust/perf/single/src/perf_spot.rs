//! Single SPOT throughput/latency benchmark.

mod common;

use std::thread;
use std::time::{Duration, Instant};

use zlink::*;

const CHANNEL_NAME_PREFIX: &str = "perf-spot-svc";
const TOPIC: &str = "bench.topic";

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

fn wait_for_registry_entries(query: &RegistryQueryClient, channel_name: &str) {
    let deadline = Instant::now() + common::resolve_single_ready_timeout();
    while Instant::now() < deadline {
        match query.snapshot(None) {
            Ok(entries)
                if entries
                    .iter()
                    .filter(|entry| entry.channel_name == channel_name)
                    .count()
                    >= 2 =>
            {
                return;
            }
            Ok(_) | Err(_) => {
                thread::sleep(Duration::from_millis(10));
            }
        }
    }
    panic!("single SPOT registry entries timed out");
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
    let channel_name = format!(
        "{CHANNEL_NAME_PREFIX}-{}-{}",
        std::process::id(),
        common::now_ns()
    );
    let Some(registry_pub_endpoint) = common::resolve_endpoint_or_emit_unsupported(
        "SPOT",
        &config.transport,
        "spot-registry-pub",
    ) else {
        return;
    };
    let Some(registry_router_endpoint) = common::resolve_endpoint_or_emit_unsupported(
        "SPOT",
        &config.transport,
        "spot-registry-router",
    ) else {
        return;
    };
    let Some(publisher_endpoint) =
        common::resolve_endpoint_or_emit_unsupported("SPOT", &config.transport, "spot-pub")
    else {
        return;
    };
    let Some(subscriber_endpoint) =
        common::resolve_endpoint_or_emit_unsupported("SPOT", &config.transport, "spot-sub")
    else {
        return;
    };

    let ctx = common::perf_context();
    let registry = Registry::new(&ctx).expect("registry");
    let publisher_discovery = Discovery::new(&ctx, AutoConnectType::SpotMesh, &channel_name)
        .expect("publisher discovery");
    let subscriber_discovery = Discovery::new(&ctx, AutoConnectType::SpotMesh, &channel_name)
        .expect("subscriber discovery");
    let query = RegistryQueryClient::new(&ctx).expect("query client");
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

    registry
        .bind(&registry_pub_endpoint, &registry_router_endpoint)
        .expect("registry bind");
    publisher_discovery
        .connect_registry(&registry_router_endpoint)
        .expect("publisher discovery connect");
    subscriber_discovery
        .connect_registry(&registry_router_endpoint)
        .expect("subscriber discovery connect");
    query
        .connect(&registry_router_endpoint)
        .expect("query connect");
    publisher_node
        .attach_discovery(&publisher_discovery)
        .expect("publisher attach discovery");
    subscriber_node
        .attach_discovery(&subscriber_discovery)
        .expect("subscriber attach discovery");
    publisher_node
        .bind(&publisher_endpoint)
        .expect("publisher bind");
    subscriber_node
        .bind(&subscriber_endpoint)
        .expect("subscriber bind");

    let publisher = publisher_node.create_spot().expect("publisher spot");
    let subscriber = subscriber_node.create_spot().expect("subscriber spot");
    subscriber
        .set_subscription("bench.")
        .expect("set subscription");

    wait_for_registry_entries(&query, &channel_name);
    wait_for_spot_ready(&publisher, &subscriber, &config);
    thread::sleep(common::resolve_single_spot_ready_settle());

    let collector = common::MetricCollector::new();
    let stats = collector.shared();
    let active_deadline = Instant::now() + Duration::from_secs(config.duration_seconds);
    // PERF_SINGLE_TEST_POLICY § 1.4: sender publishes wire-level stop token
    // at phase end (blocking, bounded attempts); receiver loops on blocking
    // `subscribe()` and exits when the stop token arrives.
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
                publisher
                    .publish(TOPIC)
                    .message(msg)
                    .submit()
                    .map(|_| ())
                    .map_err(Into::into)
            });
        }
    });

    loop {
        let mut received = TopicMessage::empty();
        match subscriber.subscribe(&mut received, RecvFlags::NONE) {
            Ok(true) => {
                let data = common::message_payload(received.parts());
                if common::is_stop_token(data) {
                    break;
                }
                common::handle_recv(data, config.size, &stats, active_deadline);
            }
            Ok(false) => continue,
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
