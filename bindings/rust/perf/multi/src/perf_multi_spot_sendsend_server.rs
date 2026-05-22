#[path = "perf_common.rs"]
mod common;

use std::io::{self, BufRead, Write};
use std::sync::{
    Arc, Mutex,
    atomic::{AtomicBool, Ordering},
    mpsc,
};
use std::thread;
use std::time::{Duration, Instant};

use zlink::*;

const PATTERN: &str = "MULTI_SPOT_SENDSEND";
const TOPIC: &str = "bench";
const SERVER_NODE_RID: &[u8] = b"SPOT-SENDSEND-SERVER-NODE";
const SERVER_SPOT_RID: &[u8] = b"SPOT-SENDSEND-SERVER-SPOT";

enum ServerEvent {
    Stop,
    RunnerStart,
    ConnectControl(String),
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

fn echo_available(spot: &Spot) {
    loop {
        let mut received = Received::empty();
        match spot.recv_routed(&mut received, RecvFlags::DONT_WAIT) {
            Ok(true) => {
                if received.request_seq().unwrap_or(0) != 0 {
                    continue;
                }
                let payload = common::message_payload(received.parts());
                let message = Message::copy_from(payload).expect("echo message");
                let _ = received
                    .send()
                    .message(message)
                    .flags(SendFlags::DONT_WAIT)
                    .submit();
            }
            Ok(false) => break,
            Err(err) if err.code() == RecvResult::NoData => break,
            Err(err) => {
                eprintln!("[spot-sendsend-server] recv error: {err}");
                break;
            }
        }
    }
}

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();
    let stop = Arc::new(AtomicBool::new(false));
    let ready_timeout = common::resolve_multi_connect_ready_timeout();
    let (event_tx, event_rx) = mpsc::channel::<ServerEvent>();

    {
        let event_tx = event_tx.clone();
        let stop = Arc::clone(&stop);
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
                    stop.store(true, Ordering::Release);
                    let _ = event_tx.send(ServerEvent::Stop);
                    return;
                }
            }
        });
    }

    let ctx = common::perf_server_context();
    common::apply_multi_auto_hwm_msg_unit(&ctx, args.msg_size);
    let data_node = SpotNode::new(&ctx).expect("spot data node");
    let control_node = SpotNode::new(&ctx).expect("spot control node");
    setup_tls_server(&data_node, &args.transport);
    setup_tls_server(&control_node, &args.transport);
    setup_tls_client(&control_node, &args.transport);
    common::apply_multi_spot_node_admission(&data_node, &settings);
    common::apply_multi_spot_node_admission(&control_node, &settings);
    data_node
        .set_routing_id(&RoutingId::from_bytes(SERVER_NODE_RID))
        .expect("data rid");

    let replier = Arc::new(Mutex::new(data_node.create_spot().expect("spot")));
    replier
        .lock()
        .expect("spot lock")
        .set_routing_id(&RoutingId::from_bytes(SERVER_SPOT_RID))
        .expect("spot rid");
    let stop_dispatch = Arc::clone(&stop);
    let dispatch_spot = Arc::clone(&replier);
    replier
        .lock()
        .expect("spot lock")
        .on_dispatch_event(move |info| {
            if stop_dispatch.load(Ordering::Acquire)
                || info.event != SpotDispatchEvent::RoutedReadable
            {
                return;
            }
            let spot = dispatch_spot.lock().expect("spot lock");
            echo_available(&spot);
        })
        .expect("dispatch event");

    let control_pub = control_node.create_spot().expect("control pub");
    let control_sub = control_node.create_spot().expect("control sub");
    control_sub.set_subscription(TOPIC).expect("control sub");

    let Some(data_bind) = common::resolve_server_bind_endpoint(PATTERN, &args.transport) else {
        return;
    };
    if let Err(err) = data_node.bind(&data_bind) {
        if common::handle_transport_setup_error(PATTERN, &args.transport, "bind", err) {
            return;
        }
        panic!("data bind: {err}");
    }
    let Some(control_bind) = common::benchmark_endpoint(
        PATTERN,
        &args.transport,
        "multi-spot-sendsend-control-server",
    ) else {
        return;
    };
    control_node.bind(&control_bind).expect("control bind");
    let data_endpoint = data_node.last_endpoint().unwrap_or(data_bind);
    let control_endpoint = control_node.last_endpoint().unwrap_or(control_bind);
    common::print_ready(&data_endpoint);
    println!("CONTROL_READY,{control_endpoint}");
    io::stdout().flush().ok();

    let mut data_connected = false;
    let mut ready_units = 0usize;
    let deadline = Instant::now() + ready_timeout;
    while Instant::now() < deadline && !stop.load(Ordering::Acquire) {
        echo_available(&replier.lock().expect("spot lock"));
        while let Ok(event) = event_rx.try_recv() {
            match event {
                ServerEvent::Stop => return,
                ServerEvent::RunnerStart => {}
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
            if let Some(endpoint) = payload.strip_prefix("DATA_ENDPOINT,") {
                data_node
                    .connect_peer(endpoint)
                    .expect("connect client data");
                data_connected = true;
            } else if let Some(rest) = payload.strip_prefix("READY_COUNT,") {
                let mut parts = rest.split(',');
                let size = parts.next().and_then(|value| value.parse::<usize>().ok());
                let count = parts.next().and_then(|value| value.parse::<usize>().ok());
                if size == Some(args.msg_size) {
                    ready_units += count.unwrap_or(0);
                }
            }
        }
        if data_connected && ready_units >= settings.clients {
            break;
        }
        thread::sleep(Duration::from_millis(1));
    }
    if !data_connected || ready_units < settings.clients {
        panic!("spot sendsend server readiness timeout");
    }

    let mut runner_start = false;
    let start_deadline = Instant::now() + ready_timeout;
    while Instant::now() < start_deadline && !runner_start {
        echo_available(&replier.lock().expect("spot lock"));
        match event_rx.recv_timeout(Duration::from_millis(1)) {
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
        panic!("spot sendsend server start handshake timeout");
    }
    if !publish_control(
        &control_pub,
        &format!("START,{}", args.msg_size),
        ready_timeout,
    ) {
        panic!("spot sendsend control start publish timeout");
    }

    let idle_deadline = Instant::now() + Duration::from_secs(settings.duration_seconds + 2);
    while Instant::now() < idle_deadline && !stop.load(Ordering::Acquire) {
        echo_available(&replier.lock().expect("spot lock"));
        thread::sleep(Duration::from_millis(1));
    }
}
