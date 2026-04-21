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
    let request_timeout =
        Duration::from_millis(settings.recv_timeout_ms.max(settings.send_timeout_ms));
    let mut latency = common::LatencyStats::new();
    let mut routers = Vec::with_capacity(settings.clients);
    let mut pollers = Vec::with_capacity(settings.clients);
    let mut payloads = Vec::with_capacity(settings.clients);
    let mut waiting_reply = vec![false; settings.clients];
    let mut send_pending = vec![false; settings.clients];
    let mut seqs = vec![1u64; settings.clients];
    for index in 0..settings.clients {
        let router = ctx.router_socket().expect("router");
        let poller = Poller::new().expect("poller");
        router
            .common_options()
            .set_send_hwm(settings.send_hwm)
            .expect("sndhwm");
        router
            .common_options()
            .set_recv_hwm(settings.recv_hwm)
            .expect("rcvhwm");
        router
            .common_options()
            .set_recv_timeout(request_timeout)
            .expect("recv timeout");
        let rid = RoutingId::from_bytes(format!("SPOT-REQ-{index}").as_bytes());
        router.set_routing_id(&rid).expect("set rid");
        if matches!(args.transport.as_str(), "tls" | "wss") {
            let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
            common::setup_raw_tls_client(&router, &tls).expect("client tls");
        }
        router.connect(data_endpoint).expect("connect");
        poller.add_socket(&router, POLLIN).expect("poller add");
        payloads.push(vec![0u8; args.msg_size.max(common::HEADER_SIZE)]);
        routers.push(router);
        pollers.push(poller);
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
        writeln!(stream, "CONNECTED").expect("write connected");
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
        for index in 0..routers.len() {
            if waiting_reply[index] || send_pending[index] {
                continue;
            }
            common::encode_header(
                &mut payloads[index],
                common::PHASE_ACTIVE,
                args.msg_size as u32,
                seqs[index],
            );
            match routers[index].send_to_spot_with_flags(
                &node_rid,
                &spot_rid,
                vec![Message::copy_from(&payloads[index]).expect("request")],
                SendFlags::DONT_WAIT,
            ) {
                Ok(()) => {
                    waiting_reply[index] = true;
                    seqs[index] += 1;
                    progressed = true;
                }
                Err(err) if err.code() == SubmitResult::Backpressured => {
                    send_pending[index] = true;
                    pollers[index]
                        .modify_socket(&routers[index], POLLIN | POLLOUT)
                        .expect("poller modify");
                }
                Err(err) if err.code() == SubmitResult::NotConnected => {}
                Err(err) => panic!("spot reqrep request failed: {err}"),
            }
        }
        if progressed {
            continue;
        }

        let mut saw_event = false;
        for index in 0..pollers.len() {
            let Some(event) = pollers[index].wait(0).expect("poller wait") else {
                continue;
            };
            saw_event = true;
            if event.is_writable() {
                send_pending[index] = false;
                pollers[index]
                    .modify_socket(&routers[index], POLLIN)
                    .expect("poller modify");
            }
            if !event.is_readable() {
                continue;
            }
            loop {
                match routers[index].recv_with_flags(RecvFlags::DONT_WAIT) {
                    Ok(received) => {
                        waiting_reply[index] = false;
                        let data = common::message_payload(received.parts());
                        if Instant::now() > deadline
                            || !common::is_valid_active_message(data, args.msg_size)
                        {
                            continue;
                        }
                        let sent_ts_ns = common::decode_sent_ts_ns(data);
                        let latency_ns =
                            common::now_ns().saturating_sub(sent_ts_ns.max(0) as u64) as f64 / 2.0;
                        latency.record_ns(latency_ns);
                    }
                    Err(err) if err.code() == RecvResult::NoData => break,
                    Err(err) => panic!("spot reqrep client recv failed: {err}"),
                }
            }
        }
        if saw_event {
            continue;
        }

        for index in 0..pollers.len() {
            let Some(event) = pollers[index].wait(1).expect("poller wait") else {
                continue;
            };
            if event.is_writable() {
                send_pending[index] = false;
                pollers[index]
                    .modify_socket(&routers[index], POLLIN)
                    .expect("poller modify");
            }
            if !event.is_readable() {
                continue;
            }
            loop {
                match routers[index].recv_with_flags(RecvFlags::DONT_WAIT) {
                    Ok(received) => {
                        waiting_reply[index] = false;
                        let data = common::message_payload(received.parts());
                        if Instant::now() > deadline
                            || !common::is_valid_active_message(data, args.msg_size)
                        {
                            continue;
                        }
                        let sent_ts_ns = common::decode_sent_ts_ns(data);
                        let latency_ns =
                            common::now_ns().saturating_sub(sent_ts_ns.max(0) as u64) as f64 / 2.0;
                        latency.record_ns(latency_ns);
                    }
                    Err(err) if err.code() == RecvResult::NoData => break,
                    Err(err) => panic!("spot reqrep client recv failed: {err}"),
                }
            }
            break;
        }
    }

    common::print_result(
        "MULTI_SPOT_REQREP",
        &args.transport,
        args.msg_size,
        settings.duration_seconds,
        &latency.finish(),
    );
}
