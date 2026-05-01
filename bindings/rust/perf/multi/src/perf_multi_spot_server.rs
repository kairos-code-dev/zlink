#[path = "perf_common.rs"]
mod common;

use std::io::{self, BufRead, BufReader, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::mpsc;
use std::thread;
use std::time::{Duration, Instant};

use zlink::*;

const SERVICE_NAME: &str = "perf-spot-svc";
const TOPIC: &str = "bench.topic";

fn env_u64(name: &str, default: u64) -> u64 {
    std::env::var(name)
        .ok()
        .and_then(|value| value.parse().ok())
        .unwrap_or(default)
}

fn control_listener() -> io::Result<(TcpListener, String)> {
    let listener = TcpListener::bind("127.0.0.1:0")?;
    let endpoint = format!(
        "tcp://127.0.0.1:{}",
        listener.local_addr().expect("control addr").port()
    );
    Ok((listener, endpoint))
}

fn tcp_addr(endpoint: &str) -> String {
    endpoint
        .strip_prefix("tcp://")
        .expect("tcp control endpoint")
        .to_string()
}

enum ServerEvent {
    Stop,
    RunnerStart,
    ClientConnected,
    ReadyCount(usize),
    StartSender(TcpStream),
}

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();
    let (event_tx, event_rx) = mpsc::channel::<ServerEvent>();

    let (listener, control_endpoint) = match control_listener() {
        Ok(value) => value,
        Err(err) => panic!("control bind: {err}"),
    };
    {
        let event_tx = event_tx.clone();
        thread::spawn(move || {
            let (stream, _) = listener.accept().expect("accept control");
            stream.set_nodelay(true).ok();
            let _ = event_tx.send(ServerEvent::StartSender(stream));
        });
    }

    {
        let event_tx = event_tx.clone();
        thread::spawn(move || {
            let stdin = io::stdin();
            for line in stdin.lock().lines() {
                let line = line.unwrap_or_default();
                let text = line.trim();
                if let Some(endpoint) = text.strip_prefix("CONNECT_CONTROL,") {
                    let stream = TcpStream::connect(tcp_addr(endpoint)).expect("connect control");
                    stream.set_nodelay(true).ok();
                    let reader = BufReader::new(stream);
                    let event_tx = event_tx.clone();
                    thread::spawn(move || {
                        for line in reader.lines() {
                            let line = line.unwrap_or_default();
                            let text = line.trim();
                            if text == "CONNECTED" {
                                let _ = event_tx.send(ServerEvent::ClientConnected);
                            } else if let Some(rest) = text.strip_prefix("READY_COUNT,") {
                                let mut parts = rest.split(',');
                                let size =
                                    parts.next().and_then(|value| value.parse::<usize>().ok());
                                let count =
                                    parts.next().and_then(|value| value.parse::<usize>().ok());
                                if size == Some(args.msg_size) {
                                    let _ =
                                        event_tx.send(ServerEvent::ReadyCount(count.unwrap_or(0)));
                                }
                            } else if matches!(text, "STOP" | "QUIT") {
                                let _ = event_tx.send(ServerEvent::Stop);
                                return;
                            }
                        }
                    });
                    continue;
                }
                if text == format!("START,{}", args.msg_size) {
                    let _ = event_tx.send(ServerEvent::RunnerStart);
                    continue;
                }
                if matches!(text, "STOP" | "QUIT") {
                    let _ = event_tx.send(ServerEvent::Stop);
                    return;
                }
            }
        });
    }

    let ctx = common::perf_server_context();
    let registry = Registry::new(&ctx).expect("registry");
    let discovery = Discovery::new(&ctx, ServiceType::Spot, SERVICE_NAME).expect("discovery");
    let node = SpotNode::new(&ctx).expect("spot node");
    common::apply_multi_spot_node_admission(&node, &settings);
    if matches!(args.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        let pem = common::load_tls_pem(&tls);
        node.set_tls_server(&pem.cert, &pem.key, false)
            .expect("spot tls");
    }
    let Some(registry_pub_endpoint) =
        common::benchmark_endpoint("MULTI_SPOT", &args.transport, "multi-spot-registry-pub")
    else {
        return;
    };
    let Some(registry_router_endpoint) =
        common::benchmark_endpoint("MULTI_SPOT", &args.transport, "multi-spot-registry-router")
    else {
        return;
    };
    let Some(data_endpoint) =
        common::benchmark_endpoint("MULTI_SPOT", &args.transport, "multi-spot-data")
    else {
        return;
    };
    registry
        .bind(&registry_pub_endpoint, &registry_router_endpoint)
        .expect("registry bind");
    discovery
        .connect_registry(&registry_router_endpoint)
        .expect("discovery connect");
    node.attach_discovery(&discovery).expect("attach discovery");
    if let Err(err) = node.bind(&data_endpoint) {
        if common::handle_transport_setup_error("MULTI_SPOT", &args.transport, "bind", err) {
            return;
        }
        panic!("bind: {err}");
    }
    let spot = node.create_spot().expect("spot");
    common::print_ready(&registry_router_endpoint);
    println!("CONTROL_READY,{control_endpoint}");
    io::stdout().flush().ok();

    let ready_settle = Duration::from_millis(env_u64("PERF_MULTI_SPOT_READY_SETTLE_MS", 1000));
    let control_settle = Duration::from_millis(env_u64("PERF_MULTI_SPOT_CONTROL_SETTLE_MS", 25));
    let ready_timeout = common::resolve_multi_connect_ready_timeout();
    let mut runner_start = false;
    let mut client_connected = false;
    let mut ready_count = 0usize;
    let mut start_sender = None::<TcpStream>;
    let control_deadline = Instant::now() + ready_timeout + ready_settle + control_settle;
    while Instant::now() < control_deadline {
        let remaining = control_deadline.saturating_duration_since(Instant::now());
        match event_rx.recv_timeout(remaining) {
            Ok(ServerEvent::Stop) => return,
            Ok(ServerEvent::RunnerStart) => runner_start = true,
            Ok(ServerEvent::ClientConnected) => client_connected = true,
            Ok(ServerEvent::ReadyCount(count)) => ready_count = count,
            Ok(ServerEvent::StartSender(stream)) => start_sender = Some(stream),
            Err(mpsc::RecvTimeoutError::Timeout) => break,
            Err(mpsc::RecvTimeoutError::Disconnected) => break,
        }
        if client_connected && start_sender.is_some() {
            break;
        }
    }
    if !client_connected || start_sender.is_none() {
        panic!("spot server control handshake timeout");
    }

    let mut warmup = vec![0u8; args.msg_size.max(common::HEADER_SIZE)];
    common::encode_header(&mut warmup, common::PHASE_WARMUP, args.msg_size as u32, 0);
    let ready_deadline = Instant::now() + ready_timeout;
    while Instant::now() < ready_deadline && ready_count < settings.clients {
        match spot.publish_with_flags(
            SERVICE_NAME,
            TOPIC,
            Message::copy_from(&warmup).expect("warmup message"),
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
            Err(err) => panic!("warmup publish: {err}"),
        }
        let remaining = ready_deadline.saturating_duration_since(Instant::now());
        let wait_slice = remaining.min(Duration::from_millis(10));
        match event_rx.recv_timeout(wait_slice) {
            Ok(ServerEvent::Stop) => return,
            Ok(ServerEvent::RunnerStart) => runner_start = true,
            Ok(ServerEvent::ClientConnected) => {}
            Ok(ServerEvent::ReadyCount(count)) => ready_count = count,
            Ok(ServerEvent::StartSender(stream)) => start_sender = Some(stream),
            Err(mpsc::RecvTimeoutError::Timeout) => {}
            Err(mpsc::RecvTimeoutError::Disconnected) => break,
        }
    }
    if ready_count < settings.clients {
        panic!("spot warmup readiness timeout");
    }

    let handshake_deadline = Instant::now() + ready_timeout;
    while Instant::now() < handshake_deadline {
        let remaining = handshake_deadline.saturating_duration_since(Instant::now());
        match event_rx.recv_timeout(remaining) {
            Ok(ServerEvent::Stop) => return,
            Ok(ServerEvent::RunnerStart) => runner_start = true,
            Ok(ServerEvent::ClientConnected) => {}
            Ok(ServerEvent::ReadyCount(_)) => {}
            Ok(ServerEvent::StartSender(stream)) => start_sender = Some(stream),
            Err(mpsc::RecvTimeoutError::Timeout) => break,
            Err(mpsc::RecvTimeoutError::Disconnected) => break,
        }
        if runner_start {
            break;
        }
    }
    if !runner_start || start_sender.is_none() {
        panic!("spot server start handshake timeout");
    }

    {
        let stream = start_sender.as_mut().expect("start sender");
        writeln!(stream, "START,{}", args.msg_size).expect("write start");
        stream.flush().expect("flush start");
    }

    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds);
    let mut buf = vec![0u8; args.msg_size.max(common::HEADER_SIZE)];
    let mut seq = 1u64;
    while Instant::now() < deadline {
        common::encode_header(&mut buf, common::PHASE_ACTIVE, args.msg_size as u32, seq);
        seq += 1;
        spot.publish(
            SERVICE_NAME,
            TOPIC,
            Message::copy_from(&buf).expect("publish msg"),
        )
        .expect("spot publish");
    }
    common::encode_header(&mut buf, common::PHASE_COOLDOWN, args.msg_size as u32, seq);
    let _ = spot.publish(
        SERVICE_NAME,
        TOPIC,
        Message::copy_from(&buf).expect("cooldown msg"),
    );
}
