#[path = "perf_common.rs"]
mod common;

use std::io::{self, BufRead, Write};
use std::sync::mpsc;
use std::thread;
use std::time::{Duration, Instant};

use zlink::*;

const TOPIC: &str = "bench";

enum ServerEvent {
    Stop,
    RunnerStart,
    ConnectControl(String),
}

fn setup_tls_server(node: &SpotNode, transport: &str) {
    if matches!(transport, "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        let pem = common::load_tls_pem(&tls);
        node.set_tls_server(&pem.cert, &pem.key, false)
            .expect("spot tls server");
    }
}

fn setup_tls_client(node: &SpotNode, transport: &str) {
    if matches!(transport, "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        let pem = common::load_tls_pem(&tls);
        node.set_tls_client(&pem.ca, "localhost", false)
            .expect("spot tls client");
    }
}

fn control_payload(control_sub: &Spot) -> Option<String> {
    match control_sub.subscribe_with_flags(RecvFlags::DONT_WAIT) {
        Ok(Some(message)) => {
            let data = common::message_payload(message.parts());
            Some(String::from_utf8_lossy(data).into_owned())
        }
        Ok(None) => None,
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
    let ready_timeout = common::resolve_multi_connect_ready_timeout();
    let (event_tx, event_rx) = mpsc::channel::<ServerEvent>();

    {
        let event_tx = event_tx.clone();
        let msg_size = args.msg_size;
        thread::spawn(move || {
            let stdin = io::stdin();
            for line in stdin.lock().lines() {
                let line = line.unwrap_or_default();
                let text = line.trim();
                if let Some(endpoint) = text.strip_prefix("CONNECT_CONTROL,") {
                    let _ = event_tx.send(ServerEvent::ConnectControl(endpoint.to_string()));
                } else if text == format!("START,{msg_size}") {
                    let _ = event_tx.send(ServerEvent::RunnerStart);
                } else if matches!(text, "STOP" | "QUIT") {
                    let _ = event_tx.send(ServerEvent::Stop);
                    return;
                }
            }
        });
    }

    let ctx = common::perf_server_context();
    let data_node = SpotNode::new(&ctx).expect("spot data node");
    let control_node = SpotNode::new(&ctx).expect("spot control node");
    setup_tls_server(&data_node, &args.transport);
    setup_tls_server(&control_node, &args.transport);
    setup_tls_client(&control_node, &args.transport);
    common::apply_multi_spot_node_admission(&data_node, &settings);
    common::apply_multi_spot_node_admission(&control_node, &settings);
    data_node
        .set_routing_id(&RoutingId::from_bytes(b"z-rust-multi-spot-server"))
        .expect("data rid");
    control_node
        .set_routing_id(&RoutingId::from_bytes(b"z-rust-multi-spot-control-server"))
        .expect("control rid");

    let data_spot = data_node.create_spot().expect("data spot");
    let control_pub = control_node.create_spot().expect("control pub");
    let control_sub = control_node.create_spot().expect("control sub");
    data_spot
        .set_routing_id(&RoutingId::from_bytes(b"z-rust-multi-spot-server-spot"))
        .expect("data spot rid");
    control_sub.set_subscription(TOPIC).expect("control sub");

    let Some(data_bind) =
        common::benchmark_endpoint("MULTI_SPOT", &args.transport, "multi-spot-data")
    else {
        return;
    };
    if let Err(err) = data_node.bind(&data_bind) {
        if common::handle_transport_setup_error("MULTI_SPOT", &args.transport, "bind", err) {
            return;
        }
        panic!("data bind: {err}");
    }
    let Some(control_bind) =
        common::benchmark_endpoint("MULTI_SPOT", &args.transport, "multi-spot-control-server")
    else {
        return;
    };
    control_node.bind(&control_bind).expect("control bind");
    let data_endpoint = data_node.last_endpoint().unwrap_or(data_bind);
    let control_endpoint = control_node.last_endpoint().unwrap_or(control_bind);
    common::print_ready(&data_endpoint);
    println!("CONTROL_READY,{control_endpoint}");
    io::stdout().flush().ok();

    let mut ready_count = 0usize;
    let mut runner_start = false;
    let deadline = Instant::now() + ready_timeout;
    while Instant::now() < deadline {
        while let Ok(event) = event_rx.try_recv() {
            match event {
                ServerEvent::Stop => return,
                ServerEvent::RunnerStart => runner_start = true,
                ServerEvent::ConnectControl(endpoint) => {
                    control_node
                        .connect_peer(&endpoint)
                        .expect("connect client control");
                    println!("CONTROL_CONNECTED,{endpoint}");
                    io::stdout().flush().ok();
                }
            }
        }
        if let Some(payload) = control_payload(&control_sub) {
            if let Some(rest) = payload.strip_prefix("READY_COUNT,") {
                let mut parts = rest.split(',');
                let size = parts.next().and_then(|value| value.parse::<usize>().ok());
                let count = parts.next().and_then(|value| value.parse::<usize>().ok());
                if size == Some(args.msg_size) {
                    ready_count += count.unwrap_or(0);
                }
            }
        }
        if ready_count >= settings.clients {
            break;
        }
        thread::sleep(Duration::from_millis(1));
    }
    if ready_count < settings.clients {
        panic!("spot control readiness timeout");
    }

    let start_deadline = Instant::now() + ready_timeout;
    while Instant::now() < start_deadline && !runner_start {
        match event_rx.recv_timeout(Duration::from_millis(10)) {
            Ok(ServerEvent::Stop) => return,
            Ok(ServerEvent::RunnerStart) => runner_start = true,
            Ok(ServerEvent::ConnectControl(endpoint)) => {
                control_node
                    .connect_peer(&endpoint)
                    .expect("connect client control");
                println!("CONTROL_CONNECTED,{endpoint}");
                io::stdout().flush().ok();
            }
            Err(mpsc::RecvTimeoutError::Timeout) => {}
            Err(mpsc::RecvTimeoutError::Disconnected) => break,
        }
    }
    if !runner_start {
        panic!("spot server start handshake timeout");
    }
    if !publish_control(
        &control_pub,
        &format!("START,{}", args.msg_size),
        ready_timeout,
    ) {
        panic!("spot control start publish timeout");
    }

    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds);
    let mut buf = vec![0u8; args.msg_size.max(common::HEADER_SIZE)];
    let mut seq = 1u64;
    while Instant::now() < deadline {
        common::encode_header(&mut buf, common::PHASE_ACTIVE, args.msg_size as u32, seq);
        seq += 1;
        match data_spot
            .publish(TOPIC)
            .message(Message::copy_from(&buf).expect("publish msg"))
            .flags(SendFlags::DONT_WAIT)
            .submit()
        {
            Ok(_) => {}
            Err(err) if err.code() == SubmitResult::Backpressured => {
                thread::sleep(Duration::from_millis(1));
            }
            Err(err) => panic!("spot publish: {err}"),
        }
    }
}
