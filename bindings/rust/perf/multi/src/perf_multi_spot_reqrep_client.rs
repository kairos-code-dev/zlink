#[path = "perf_common.rs"]
mod common;

use std::io::{self, BufRead, BufReader, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::mpsc;
use std::thread;
use std::time::{Duration, Instant};

use zlink::*;

const SERVER_NODE_RID: &[u8] = b"perf-spot-reqrep-server-node";
const SERVER_SPOT_RID: &[u8] = b"perf-spot-reqrep-server-spot";

fn env_u64(name: &str, default: u64) -> u64 {
    std::env::var(name)
        .ok()
        .and_then(|value| value.parse().ok())
        .unwrap_or(default)
}

fn control_listener() -> (TcpListener, String) {
    let listener = TcpListener::bind("127.0.0.1:0").expect("client control bind");
    let endpoint = format!(
        "tcp://127.0.0.1:{}",
        listener.local_addr().expect("client control addr").port()
    );
    (listener, endpoint)
}

fn tcp_addr(endpoint: &str) -> String {
    endpoint
        .strip_prefix("tcp://")
        .expect("tcp control endpoint")
        .to_string()
}

enum ClientEvent {
    ReadySender(TcpStream),
    RunnerStart,
    Started,
    Stop,
}

fn request_spot_reply(
    spot: &Spot,
    node_rid: RoutingId,
    spot_rid: RoutingId,
    msg: Message,
    timeout: Duration,
) -> Option<Vec<Message>> {
    let (tx, rx) = mpsc::channel();
    let submit = spot
        .request_to_spot(node_rid, spot_rid)
        .message(msg)
        .timeout(timeout)
        .submit_callback(move |result| {
            let _ = tx.send(result);
        });
    match submit.and_then(|_| {
        rx.recv()
            .map_err(|_| SubmitError::new(SubmitResult::InternalError, libc::EINVAL))?
            .map_err(|err| SubmitError::new(SubmitResult::InternalError, err.internal_errno()))
    }) {
        Ok(parts) => Some(parts),
        Err(err) => {
            if std::env::var("PERF_RUST_MULTI_SPOT_REQREP_TRACE")
                .ok()
                .as_deref()
                == Some("1")
            {
                eprintln!("spot reqrep request failed: {err}");
            }
            None
        }
    }
}

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();
    let mut endpoint_parts = args.endpoint.splitn(2, ',');
    let data_endpoint = endpoint_parts.next().expect("data endpoint");
    let control_endpoint = endpoint_parts.next().expect("control endpoint");

    let ready_settle = Duration::from_millis(env_u64("PERF_MULTI_SPOT_READY_SETTLE_MS", 1000));
    let control_settle = Duration::from_millis(env_u64("PERF_MULTI_SPOT_CONTROL_SETTLE_MS", 25));
    let ready_timeout = common::resolve_multi_connect_ready_timeout();
    let (event_tx, event_rx) = mpsc::channel::<ClientEvent>();

    let (listener, client_control_endpoint) = control_listener();
    println!("CLIENT_CONTROL_ENDPOINT,{client_control_endpoint}");
    io::stdout().flush().ok();

    {
        let event_tx = event_tx.clone();
        let client_control_endpoint = client_control_endpoint.clone();
        thread::spawn(move || {
            let (stream, _) = listener.accept().expect("accept server control");
            stream.set_nodelay(true).ok();
            println!("CONTROL_CONNECTED,{client_control_endpoint}");
            io::stdout().flush().ok();
            let _ = event_tx.send(ClientEvent::ReadySender(stream));
        });
    }

    {
        let event_tx = event_tx.clone();
        thread::spawn(move || {
            let stdin = io::stdin();
            for line in stdin.lock().lines() {
                let line = line.unwrap_or_default();
                let text = line.trim();
                if text == format!("START,{}", args.msg_size) {
                    let _ = event_tx.send(ClientEvent::RunnerStart);
                } else if matches!(text, "STOP" | "QUIT") {
                    let _ = event_tx.send(ClientEvent::Stop);
                    return;
                }
            }
        });
    }

    {
        let event_tx = event_tx.clone();
        let control_endpoint = control_endpoint.to_string();
        thread::spawn(move || {
            let stream = TcpStream::connect(tcp_addr(&control_endpoint)).expect("connect control");
            let reader = BufReader::new(stream);
            for line in reader.lines() {
                let line = line.unwrap_or_default();
                if line.trim() == format!("START,{}", args.msg_size) {
                    let _ = event_tx.send(ClientEvent::Started);
                    return;
                }
            }
        });
    }

    let node_rid = RoutingId::from_bytes(SERVER_NODE_RID);
    let spot_rid = RoutingId::from_bytes(SERVER_SPOT_RID);
    let ctx = common::perf_client_context();
    let mut latency = common::LatencyStats::new();
    let mut spots: Vec<Box<Spot>> = Vec::with_capacity(settings.clients);
    let mut data_endpoints = Vec::with_capacity(settings.clients);
    let mut payloads = Vec::with_capacity(settings.clients);
    let mut seqs = vec![1u64; settings.clients];
    let node = SpotNode::new(&ctx).expect("spot node");
    common::apply_multi_spot_node_admission(&node, &settings);
    node.set_routing_id(&RoutingId::from_bytes(b"spot-req-client-node"))
        .expect("node rid");
    if matches!(args.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        let pem = common::load_tls_pem(&tls);
        node.set_tls_client(&pem.ca, "localhost", false)
            .expect("spot tls");
    }
    let Some(bind_endpoint) = common::benchmark_endpoint(
        "MULTI_SPOT_REQREP",
        &args.transport,
        "multi-spot-reqrep-client",
    ) else {
        return;
    };
    node.bind(&bind_endpoint).expect("client bind");
    data_endpoints.push(node.last_endpoint().expect("client endpoint"));
    node.connect_peer(data_endpoint).expect("connect peer");

    for index in 0..settings.clients {
        let spot = Box::new(node.create_spot().expect("spot"));
        spot.set_routing_id(&RoutingId::from_bytes(
            format!("spot-req-client-spot-{index}").as_bytes(),
        ))
        .expect("spot rid");
        payloads.push(vec![0u8; args.msg_size.max(common::HEADER_SIZE)]);
        spots.push(spot);
    }

    let mut ready_sender = None::<TcpStream>;
    let ready_deadline = Instant::now() + ready_timeout + ready_settle + control_settle;
    while Instant::now() < ready_deadline {
        let remaining = ready_deadline.saturating_duration_since(Instant::now());
        match event_rx.recv_timeout(remaining) {
            Ok(ClientEvent::ReadySender(stream)) => ready_sender = Some(stream),
            Ok(ClientEvent::Stop) => return,
            Ok(ClientEvent::RunnerStart | ClientEvent::Started) => {}
            Err(mpsc::RecvTimeoutError::Timeout) => break,
            Err(mpsc::RecvTimeoutError::Disconnected) => break,
        }
        if ready_sender.is_some() {
            break;
        }
    }
    if ready_sender.is_none() {
        panic!("spot reqrep client control connection timeout");
    }

    thread::sleep(ready_settle);
    thread::sleep(control_settle);

    {
        let stream = ready_sender.as_mut().expect("ready sender");
        for endpoint in &data_endpoints {
            writeln!(stream, "DATA_ENDPOINT,{endpoint}").expect("write data endpoint");
        }
        stream.flush().expect("flush data endpoints");
    }

    thread::sleep(control_settle);

    {
        let stream = ready_sender.as_mut().expect("ready sender");
        writeln!(stream, "CONNECTED").expect("write connected");
        stream.flush().expect("flush connected");
    }

    let probe_deadline = Instant::now() + ready_timeout;
    let mut probe = vec![0u8; args.msg_size.max(common::HEADER_SIZE)];
    common::encode_header(&mut probe, common::PHASE_WARMUP, args.msg_size as u32, 0);
    let mut probe_ready = false;
    while Instant::now() < probe_deadline {
        if request_spot_reply(
            &spots[0],
            node_rid.clone(),
            spot_rid.clone(),
            Message::copy_from(&probe).expect("probe"),
            Duration::from_millis(settings.recv_timeout_ms.max(settings.send_timeout_ms)),
        )
        .is_some()
        {
            probe_ready = true;
            break;
        }
        thread::sleep(Duration::from_millis(10));
    }
    if !probe_ready {
        panic!("spot reqrep probe-ready timeout");
    }

    {
        let stream = ready_sender.as_mut().expect("ready sender");
        writeln!(stream, "READY_COUNT,{},{}", args.msg_size, settings.clients)
            .expect("write ready count");
        stream.flush().expect("flush ready count");
    }

    println!("CLIENT_READY,{}", args.msg_size);
    io::stdout().flush().ok();

    let mut runner_start = false;
    let mut started = false;
    let start_deadline = Instant::now() + ready_timeout;
    while Instant::now() < start_deadline {
        let remaining = start_deadline.saturating_duration_since(Instant::now());
        match event_rx.recv_timeout(remaining) {
            Ok(ClientEvent::RunnerStart) => runner_start = true,
            Ok(ClientEvent::Started) => started = true,
            Ok(ClientEvent::Stop) => return,
            Ok(ClientEvent::ReadySender(_)) => {}
            Err(mpsc::RecvTimeoutError::Timeout) => break,
            Err(mpsc::RecvTimeoutError::Disconnected) => break,
        }
        if runner_start && started {
            break;
        }
    }
    if !runner_start || !started {
        panic!("spot reqrep client start handshake timeout");
    }

    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds);
    while Instant::now() < deadline {
        let mut progressed = false;
        for index in 0..spots.len() {
            common::encode_header(
                &mut payloads[index],
                common::PHASE_ACTIVE,
                args.msg_size as u32,
                seqs[index],
            );
            let Some(reply) = request_spot_reply(
                &spots[index],
                node_rid.clone(),
                spot_rid.clone(),
                Message::copy_from(&payloads[index]).expect("request"),
                Duration::from_millis(settings.recv_timeout_ms.max(settings.send_timeout_ms)),
            ) else {
                continue;
            };
            let data = common::message_payload(&reply);
            if Instant::now() <= deadline && common::is_valid_active_message(data, args.msg_size) {
                let sent_ts_ns = common::decode_sent_ts_ns(data);
                let latency_ns =
                    common::now_ns().saturating_sub(sent_ts_ns.max(0) as u64) as f64 / 2.0;
                latency.record_ns(latency_ns);
                seqs[index] += 1;
                progressed = true;
            } else if std::env::var("PERF_RUST_MULTI_SPOT_REQREP_TRACE")
                .ok()
                .as_deref()
                == Some("1")
            {
                eprintln!(
                    "spot reqrep invalid reply len={} magic={} phase={} msg_size={}",
                    data.len(),
                    common::decode_magic(data),
                    common::decode_phase(data),
                    common::decode_msg_size(data)
                );
            }
        }
        if progressed {
            continue;
        }
        thread::sleep(Duration::from_millis(1));
    }

    common::print_result(
        "MULTI_SPOT_REQREP",
        &args.transport,
        args.msg_size,
        settings.duration_seconds,
        &latency.finish(),
    );
}
