#[path = "perf_common.rs"]
mod common;

use std::io::{self, BufRead, BufReader, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::{
    Arc,
    atomic::{AtomicBool, Ordering},
    mpsc,
};
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
    DataEndpoint(String),
    ReadyCount,
    StartSender(TcpStream),
}

fn trace_enabled() -> bool {
    std::env::var("PERF_RUST_MULTI_SPOT_REQREP_TRACE")
        .ok()
        .as_deref()
        == Some("1")
}

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();
    let stop = Arc::new(AtomicBool::new(false));
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
        let stop = Arc::clone(&stop);
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
                    let stop_reader = Arc::clone(&stop);
                    thread::spawn(move || {
                        for line in reader.lines() {
                            let line = line.unwrap_or_default();
                            let text = line.trim();
                            if text == "CONNECTED" {
                                let _ = event_tx.send(ServerEvent::ClientConnected);
                            } else if let Some(endpoint) = text.strip_prefix("DATA_ENDPOINT,") {
                                let _ =
                                    event_tx.send(ServerEvent::DataEndpoint(endpoint.to_string()));
                            } else if let Some(rest) = text.strip_prefix("READY_COUNT,") {
                                let mut parts = rest.split(',');
                                let size =
                                    parts.next().and_then(|value| value.parse::<usize>().ok());
                                let count =
                                    parts.next().and_then(|value| value.parse::<usize>().ok());
                                if size == Some(args.msg_size) {
                                    let _ = count;
                                    let _ = event_tx.send(ServerEvent::ReadyCount);
                                }
                            } else if matches!(text, "STOP" | "QUIT") {
                                let _ = event_tx.send(ServerEvent::Stop);
                                return;
                            }
                            if stop_reader.load(Ordering::Acquire) {
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
                    stop.store(true, Ordering::Release);
                    let _ = event_tx.send(ServerEvent::Stop);
                    return;
                }
            }
        });
    }

    let ctx = common::perf_server_context();
    let node = SpotNode::new(&ctx).expect("spot node");
    common::apply_multi_spot_node_admission(&node, &settings);
    node.set_routing_id(&RoutingId::from_bytes(SERVER_NODE_RID))
        .expect("node routing id");
    if matches!(args.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        let pem = common::load_tls_pem(&tls);
        node.set_tls_server(&pem.cert, &pem.key, false)
            .expect("spot tls");
    }
    let mut spot = node.create_spot().expect("spot");
    spot.set_routing_id(&RoutingId::from_bytes(SERVER_SPOT_RID))
        .expect("spot routing id");
    let stop_dispatch = Arc::clone(&stop);
    let spot_ptr = (&spot as *const Spot) as usize;
    spot.on_dispatch_event(move |info| {
        if stop_dispatch.load(Ordering::Acquire) || info.event != SpotDispatchEvent::RoutedReadable
        {
            return;
        }
        let spot = unsafe { &*(spot_ptr as *const Spot) };
        loop {
            match spot.recv_routed_with_flags(RecvFlags::DONT_WAIT) {
                Ok(received) => {
                    if trace_enabled() {
                        eprintln!(
                            "spot reqrep server received routed request node_len={} spot_len={} seq={}",
                            received.routing_id().map(|rid| rid.size()).unwrap_or(0),
                            received.spot_rid.as_ref().map(|rid| rid.size()).unwrap_or(0),
                            received.request_seq().unwrap_or(0)
                        );
                    }
                    let reply = Message::copy_from(common::message_payload(received.parts()))
                        .expect("reply");
                    if let Err(err) = received.reply(vec![reply]) {
                        if trace_enabled() {
                            eprintln!("spot reqrep reply failed: {err}");
                        }
                    }
                }
                Err(err) if err.code() == RecvResult::NoData => break,
                Err(err) => {
                    eprintln!("[spot-reqrep-server] recv error in dispatch: {:?}", err);
                    break;
                }
            }
        }
    })
    .expect("dispatch event");
    let Some(data_bind_endpoint) =
        common::resolve_server_bind_endpoint("MULTI_SPOT_REQREP", &args.transport)
    else {
        return;
    };
    if let Err(err) = node.bind(&data_bind_endpoint) {
        if common::handle_transport_setup_error("MULTI_SPOT_REQREP", &args.transport, "bind", err) {
            return;
        }
        panic!("bind: {err}");
    }
    let endpoint = node.last_endpoint().expect("endpoint");
    common::print_ready(&endpoint);
    println!("CONTROL_READY,{control_endpoint}");
    io::stdout().flush().ok();

    let ready_settle = Duration::from_millis(env_u64("PERF_MULTI_SPOT_READY_SETTLE_MS", 1000));
    let control_settle = Duration::from_millis(env_u64("PERF_MULTI_SPOT_CONTROL_SETTLE_MS", 25));
    let ready_timeout = common::resolve_multi_connect_ready_timeout();
    let mut runner_start = false;
    let mut client_connected = false;
    let mut data_endpoint_count = 0usize;
    let mut start_sender = None::<TcpStream>;
    let control_deadline = Instant::now() + ready_timeout + ready_settle + control_settle;
    while Instant::now() < control_deadline {
        let remaining = control_deadline
            .saturating_duration_since(Instant::now())
            .min(Duration::from_millis(10));
        match event_rx.recv_timeout(remaining) {
            Ok(ServerEvent::Stop) => return,
            Ok(ServerEvent::RunnerStart) => runner_start = true,
            Ok(ServerEvent::ClientConnected) => client_connected = true,
            Ok(ServerEvent::DataEndpoint(endpoint)) => {
                if trace_enabled() {
                    eprintln!("spot reqrep server connect data endpoint: {endpoint}");
                }
                node.connect_peer(&endpoint).expect("connect client data");
                data_endpoint_count += 1;
            }
            Ok(ServerEvent::ReadyCount) => {}
            Ok(ServerEvent::StartSender(stream)) => start_sender = Some(stream),
            Err(mpsc::RecvTimeoutError::Timeout) => continue,
            Err(mpsc::RecvTimeoutError::Disconnected) => break,
        }
        if client_connected && data_endpoint_count >= 1 && start_sender.is_some() {
            break;
        }
    }
    if !client_connected || data_endpoint_count < 1 || start_sender.is_none() {
        panic!("spot reqrep server control handshake timeout");
    }

    let start_deadline = Instant::now() + ready_timeout;
    while Instant::now() < start_deadline {
        let remaining = start_deadline
            .saturating_duration_since(Instant::now())
            .min(Duration::from_millis(10));
        match event_rx.recv_timeout(remaining) {
            Ok(ServerEvent::Stop) => return,
            Ok(ServerEvent::RunnerStart) => runner_start = true,
            Ok(ServerEvent::ClientConnected) => {}
            Ok(ServerEvent::DataEndpoint(endpoint)) => {
                if trace_enabled() {
                    eprintln!("spot reqrep server connect late data endpoint: {endpoint}");
                }
                node.connect_peer(&endpoint).expect("connect client data");
            }
            Ok(ServerEvent::ReadyCount) => {}
            Ok(ServerEvent::StartSender(stream)) => start_sender = Some(stream),
            Err(mpsc::RecvTimeoutError::Timeout) => continue,
            Err(mpsc::RecvTimeoutError::Disconnected) => break,
        }
        if runner_start {
            break;
        }
    }
    if !runner_start {
        panic!("spot reqrep server start handshake timeout");
    }

    {
        let stream = start_sender.as_mut().expect("start sender");
        writeln!(stream, "START,{}", args.msg_size).expect("write start");
        stream.flush().expect("flush start");
    }

    while !stop.load(Ordering::Acquire) {
        while let Ok(event) = event_rx.try_recv() {
            match event {
                ServerEvent::Stop => {
                    stop.store(true, Ordering::Release);
                    break;
                }
                ServerEvent::DataEndpoint(endpoint) => {
                    if trace_enabled() {
                        eprintln!("spot reqrep server connect runtime data endpoint: {endpoint}");
                    }
                    node.connect_peer(&endpoint).expect("connect client data");
                }
                ServerEvent::RunnerStart
                | ServerEvent::ClientConnected
                | ServerEvent::ReadyCount
                | ServerEvent::StartSender(_) => {}
            }
        }
        if stop.load(Ordering::Acquire) {
            break;
        }
        thread::sleep(Duration::from_millis(1));
    }
}
