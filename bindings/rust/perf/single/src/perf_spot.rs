//! Single SPOT throughput/latency benchmark.

mod common;

use std::thread;
use std::time::{Duration, Instant};

use zlink::*;

const SERVICE_NAME: &str = "perf-spot-svc";
const TOPIC: &str = "bench.topic";

fn drain_spot_readable(
    subscriber: &Spot,
    config: &common::PerfConfig,
    stats: Option<&std::sync::Arc<std::sync::Mutex<common::LatencyStats>>>,
    collect_active: bool,
) -> bool {
    let mut processed = false;
    loop {
        match subscriber.subscribe_with_flags(RecvFlags::DONT_WAIT) {
            Ok(received) => {
                {
                    let data = common::message_payload(received.parts());
                    if collect_active {
                        if let Some(stats) = stats {
                            common::handle_recv(data, config.size, stats);
                        }
                    }
                }
                drop(received);
                processed = true;
            }
            Err(err)
                if err.code() == RecvResult::NoData || err.internal_errno() == libc::ENOENT =>
            {
                break;
            }
            Err(err) => panic!("spot subscribe drain failed: {err}"),
        }
    }
    processed
}

fn wait_for_spot_ready(publisher: &Spot, subscriber: &Spot, config: &common::PerfConfig) {
    let deadline = Instant::now() + common::resolve_single_ready_timeout();
    let mut probe = vec![0u8; config.size.max(common::HEADER_SIZE)];
    common::encode_header(&mut probe, common::PHASE_WARMUP, config.size as u32, 0);
    while Instant::now() < deadline {
        match publisher.publish_with_flags(
            SERVICE_NAME,
            TOPIC,
            Message::copy_from(&probe).expect("probe message"),
            SendFlags::DONT_WAIT,
        ) {
            Ok(()) => {}
            Err(err)
                if matches!(
                    err.code(),
                    SubmitResult::NotConnected
                        | SubmitResult::NotFound
                        | SubmitResult::Backpressured
                ) => {}
            Err(err) => panic!("probe publish: {err}"),
        }
        if drain_spot_readable(subscriber, config, None, false) {
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
        .unwrap_or(50);
    Duration::from_micros(micros)
}

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let Some(registry_pub_endpoint) =
        common::resolve_endpoint_or_emit_unsupported("SPOT", &config.transport, "spot-registry-pub")
    else {
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
    let discovery = Discovery::new(&ctx, ServiceType::Spot, SERVICE_NAME).expect("discovery");
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
    discovery
        .connect_registry(&registry_router_endpoint)
        .expect("discovery connect");
    publisher_node
        .attach_discovery(&discovery)
        .expect("publisher attach discovery");
    subscriber_node
        .attach_discovery(&discovery)
        .expect("subscriber attach discovery");
    publisher_node.bind(&publisher_endpoint).expect("publisher bind");
    subscriber_node
        .bind(&subscriber_endpoint)
        .expect("subscriber bind");

    let publisher = publisher_node.create_spot().expect("publisher spot");
    let subscriber = subscriber_node.create_spot().expect("subscriber spot");
    subscriber
        .set_subscription("bench.")
        .expect("set subscription");

    wait_for_spot_ready(&publisher, &subscriber, &config);
    thread::sleep(common::resolve_single_spot_ready_settle());

    let collector = common::MetricCollector::new();
    let stats = collector.shared();
    let active_deadline = Instant::now() + Duration::from_secs(config.duration_seconds);
    let publisher_thread = thread::spawn({
        let send_gap = spot_send_gap();
        move || {
            let mut seq: u64 = 0;
            let mut payload = vec![0u8; config.size.max(common::HEADER_SIZE)];
            while Instant::now() < active_deadline {
                common::encode_header(
                    &mut payload,
                    common::PHASE_ACTIVE,
                    config.size as u32,
                    seq,
                );
                match publisher.publish_with_flags(
                    SERVICE_NAME,
                    TOPIC,
                    Message::copy_from(&payload).expect("active message"),
                    SendFlags::DONT_WAIT,
                ) {
                    Ok(()) => {
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

            common::encode_header(
                &mut payload,
                common::PHASE_COOLDOWN,
                config.size as u32,
                seq,
            );
            let _ = publisher.publish_with_flags(
                SERVICE_NAME,
                TOPIC,
                Message::copy_from(&payload).expect("cooldown message"),
                SendFlags::DONT_WAIT,
            );
        }
    });

    while Instant::now() < active_deadline {
        if !drain_spot_readable(&subscriber, &config, Some(&stats), true) {
            thread::sleep(Duration::from_millis(1));
        }
    }
    publisher_thread.join().expect("publisher thread");

    let idle_deadline = Instant::now()
        + Duration::from_millis(common::resolve_single_idle_drain_ms());
    while Instant::now() < idle_deadline {
        if !drain_spot_readable(&subscriber, &config, None, false) {
            thread::sleep(Duration::from_millis(1));
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
