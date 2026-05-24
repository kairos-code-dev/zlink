#[path = "perf_common.rs"]
mod common;

use std::io::{self, BufRead, Write};
use std::sync::mpsc;
use std::thread;
use std::time::{Duration, Instant};

use zlink::*;

const TOPIC: &str = "bench";

enum ClientEvent {
    RunnerControlConnected,
    RunnerStart,
    Stop,
}

fn env_u64(name: &str, default: u64) -> u64 {
    std::env::var(name)
        .ok()
        .and_then(|value| value.parse().ok())
        .unwrap_or(default)
}

fn setup_tls_server(node: &SpotNode, transport: &str) {
    if matches!(transport, "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        node.set_tls_server(&tls.cert, &tls.key, false)
            .expect("spot tls server");
    }
}

fn setup_tls_client(node: &SpotNode, transport: &str) {
    if matches!(transport, "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        node.set_tls_client(&tls.ca, "localhost", false)
            .expect("spot tls client");
    }
}

fn split_endpoint(endpoint: &str) -> (&str, &str) {
    endpoint
        .split_once(',')
        .expect("spot client expects data,control endpoint")
}

fn control_payload(control_sub: &Spot) -> Option<String> {
    let mut message = TopicMessage::empty();
    match control_sub.subscribe(&mut message, RecvFlags::DONT_WAIT) {
        Ok(true) => {
            let data = common::message_payload(message.parts());
            Some(String::from_utf8_lossy(data).into_owned())
        }
        Ok(false) => None,
        Err(err) if err.code() == RecvResult::NoData => None,
        Err(err) => panic!("control subscribe failed: {err}"),
    }
}

fn publish_control(control_pub: &Spot, payload: &str, timeout: Duration) -> bool {
    let deadline = Instant::now() + timeout;
    while Instant::now() < deadline {
        match control_pub
            .publish(TOPIC)
            .message(Message::copy_from(payload.as_bytes()).expect("control message"))
            .flags(SendFlags::DONT_WAIT)
            .submit()
        {
            Ok(_) => return true,
            Err(err) if err.code() == SubmitResult::Backpressured => {
                thread::sleep(Duration::from_millis(1));
            }
            Err(err) => panic!("control publish failed: {err}"),
        }
    }
    false
}

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();
    let (data_endpoint, control_endpoint) = split_endpoint(&args.endpoint);
    let data_endpoint = data_endpoint.to_string();
    let control_endpoint = control_endpoint.to_string();
    let ready_settle = Duration::from_millis(env_u64("PERF_MULTI_SPOT_READY_SETTLE_MS", 1000));
    let control_settle = Duration::from_millis(env_u64("PERF_MULTI_SPOT_CONTROL_SETTLE_MS", 25));
    let ready_timeout = common::resolve_multi_connect_ready_timeout();
    let (event_tx, event_rx) = mpsc::channel::<ClientEvent>();

    {
        let event_tx = event_tx.clone();
        let msg_size = args.msg_size;
        thread::spawn(move || {
            let stdin = io::stdin();
            for line in stdin.lock().lines() {
                let line = line.unwrap_or_default();
                let text = line.trim();
                if text == format!("START,{msg_size}") {
                    let _ = event_tx.send(ClientEvent::RunnerStart);
                } else if text.starts_with("CONTROL_CONNECTED,") {
                    let _ = event_tx.send(ClientEvent::RunnerControlConnected);
                } else if matches!(text, "STOP" | "QUIT") {
                    let _ = event_tx.send(ClientEvent::Stop);
                    return;
                }
            }
        });
    }

    let ctx = common::perf_client_context();
    common::apply_multi_auto_hwm_msg_unit(&ctx, args.msg_size);
    let data_node = SpotNode::new(&ctx).expect("spot data node");
    let control_node = SpotNode::new(&ctx).expect("spot control node");
    setup_tls_client(&data_node, &args.transport);
    setup_tls_server(&control_node, &args.transport);
    setup_tls_client(&control_node, &args.transport);
    common::apply_multi_spot_node_admission(&data_node, &settings);
    common::apply_multi_spot_node_admission(&control_node, &settings);
    data_node
        .set_routing_id(&RoutingId::from_bytes(b"a-rust-multi-spot-client"))
        .expect("data rid");
    control_node
        .set_routing_id(&RoutingId::from_bytes(b"a-rust-multi-spot-control-client"))
        .expect("control rid");

    let control_pub = control_node.create_spot().expect("control pub");
    let control_sub = control_node.create_spot().expect("control sub");
    control_sub.set_subscription(TOPIC).expect("control sub");
    let Some(control_bind) =
        common::benchmark_endpoint("MULTI_SPOT", &args.transport, "multi-spot-control-client")
    else {
        return;
    };
    control_node
        .set_pub_bind(&control_bind)
        .expect("control bind");
    control_node
        .connect_peer(&control_endpoint)
        .expect("connect server control");
    let client_control_endpoint = control_node.last_endpoint().unwrap_or(control_bind);
    println!("CLIENT_CONTROL_ENDPOINT,{client_control_endpoint}");
    io::stdout().flush().ok();

    data_node
        .connect_peer(&data_endpoint)
        .expect("connect data");
    let mut spots: Vec<Box<Spot>> = Vec::with_capacity(settings.clients);
    for index in 0..settings.clients {
        let spot = Box::new(data_node.create_spot().expect("spot"));
        spot.set_routing_id(&RoutingId::from_bytes(
            format!("a-rust-multi-spot-client-spot-{index}").as_bytes(),
        ))
        .expect("spot rid");
        spot.set_subscription(TOPIC).expect("subscription");
        spots.push(spot);
    }

    let mut runner_control_connected = false;
    let ready_deadline = Instant::now() + ready_timeout;
    while Instant::now() < ready_deadline && !runner_control_connected {
        match event_rx.recv_timeout(Duration::from_millis(10)) {
            Ok(ClientEvent::RunnerControlConnected) => runner_control_connected = true,
            Ok(ClientEvent::Stop) => return,
            Ok(ClientEvent::RunnerStart) => {}
            Err(mpsc::RecvTimeoutError::Timeout) => {}
            Err(mpsc::RecvTimeoutError::Disconnected) => break,
        }
    }
    if !runner_control_connected {
        panic!("spot client control connection timeout");
    }

    thread::sleep(ready_settle);
    thread::sleep(control_settle);
    if !publish_control(
        &control_pub,
        &format!("READY_COUNT,{},{}", args.msg_size, settings.clients),
        ready_timeout,
    ) {
        panic!("spot ready publish timeout");
    }
    println!("CLIENT_READY,{}", args.msg_size);
    io::stdout().flush().ok();

    let mut runner_start = false;
    let start_deadline = Instant::now() + ready_timeout;
    while Instant::now() < start_deadline && !runner_start {
        match event_rx.recv_timeout(Duration::from_millis(10)) {
            Ok(ClientEvent::RunnerStart) => runner_start = true,
            Ok(ClientEvent::Stop) => return,
            Ok(ClientEvent::RunnerControlConnected) => {}
            Err(mpsc::RecvTimeoutError::Timeout) => {}
            Err(mpsc::RecvTimeoutError::Disconnected) => break,
        }
    }
    if !runner_start {
        panic!("spot client runner start handshake timeout");
    }

    let direct_deadline = Instant::now() + ready_timeout;
    let mut direct_started = false;
    while Instant::now() < direct_deadline {
        if let Some(payload) = control_payload(&control_sub) {
            if payload == format!("START,{}", args.msg_size) {
                direct_started = true;
                break;
            }
        }
        thread::sleep(Duration::from_millis(1));
    }
    if !direct_started {
        panic!("spot client direct start handshake timeout");
    }

    let active_deadline = Instant::now() + Duration::from_secs(settings.duration_seconds);
    let msg_size = args.msg_size;
    let use_poller_wait = use_multi_spot_poller_wait(&args.transport, args.msg_size);
    // Parallelize the subscriber drain across worker threads, mirroring the Go
    // MULTI_SPOT recv workers. A single thread draining N spots sequentially is
    // bound by per-subscribe cgo cost; splitting the spots across a few threads
    // lets independent CPUs absorb the broadcast fan-out. Spot is Send, so each
    // scoped thread takes a disjoint &mut [Spot] chunk (no cross-thread sharing
    // of a single spot).
    let worker_count = std::env::var("PERF_MULTI_SPOT_RECV_WORKERS")
        .ok()
        .and_then(|v| v.parse::<usize>().ok())
        .filter(|&n| n > 0)
        .unwrap_or_else(|| default_multi_spot_recv_workers(&args.transport, args.msg_size))
        .min(spots.len().max(1));
    let chunk_size = spots.len().div_ceil(worker_count.max(1));
    let mut stats = common::LatencyStats::new();
    std::thread::scope(|scope| {
        let mut handles = Vec::new();
        for chunk in spots.chunks_mut(chunk_size.max(1)) {
            handles.push(scope.spawn(move || {
                let mut local = common::LatencyStats::new();
                let mut received = TopicMessage::empty();
                let poller = if use_poller_wait {
                    let poller = Poller::new().expect("spot client poller");
                    for (index, spot) in chunk.iter().enumerate() {
                        poller
                            .add_socket(&**spot, POLLIN, index)
                            .expect("spot client poller add");
                    }
                    Some(poller)
                } else {
                    None
                };
                let mut poll_events = vec![PollEvent::default(); chunk.len().max(1)];
                while Instant::now() < active_deadline {
                    let mut progressed = false;
                    for spot in chunk.iter() {
                        loop {
                            if Instant::now() >= active_deadline {
                                break;
                            }
                            match spot.subscribe(&mut received, RecvFlags::DONT_WAIT) {
                                Ok(true) => {
                                    progressed = true;
                                    // Decode the metric header from the borrowed
                                    // payload directly (no per-message copy).
                                    let data = common::message_payload(received.parts());
                                    if Instant::now() <= active_deadline
                                        && common::is_valid_active_message(data, msg_size)
                                    {
                                        let sent_ts_ns = common::decode_sent_ts_ns(data);
                                        let now_ns = common::now_ns();
                                        if sent_ts_ns > 0 && now_ns >= sent_ts_ns as u64 {
                                            local.record_ns((now_ns - sent_ts_ns as u64) as f64);
                                        }
                                    }
                                }
                                Ok(false) => break,
                                Err(err) if err.code() == RecvResult::NoData => break,
                                Err(err) => panic!("spot client subscribe failed: {err}"),
                            }
                        }
                    }
                    if !progressed {
                        if let Some(poller) = &poller {
                            let remaining_ms = active_deadline
                                .saturating_duration_since(Instant::now())
                                .as_millis()
                                .min(50)
                                .max(1) as i64;
                            match poller.wait(&mut poll_events, remaining_ms) {
                                Ok(_) => {}
                                Err(err) => panic!("spot client poller wait failed: {err}"),
                            }
                        } else {
                            thread::sleep(Duration::from_millis(1));
                        }
                    }
                }
                local
            }));
        }
        for handle in handles {
            stats.merge(handle.join().expect("spot recv worker"));
        }
    });

    let stats = stats.finish();
    common::print_result(
        "MULTI_SPOT",
        &args.transport,
        args.msg_size,
        settings.duration_seconds,
        &stats,
    );
}

fn use_multi_spot_poller_wait(transport: &str, msg_size: usize) -> bool {
    match transport {
        "tcp" => msg_size == 131072,
        "ws" => msg_size == 131072 || msg_size == 262144,
        _ => false,
    }
}

fn default_multi_spot_recv_workers(transport: &str, msg_size: usize) -> usize {
    if (transport == "tcp" || transport == "ws") && msg_size == 131072 {
        8
    } else if transport == "ws" && msg_size >= 262144 {
        7
    } else {
        4
    }
}
