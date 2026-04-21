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
    ReadyCount(usize),
    StartSender(TcpStream),
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
    spot.on_dispatch_event(move |event| {
        if stop_dispatch.load(Ordering::Acquire) || event != SpotDispatchEvent::RoutedReadable {
            return;
        }
        let spot = unsafe { &*(spot_ptr as *const Spot) };
        loop {
            match spot.recv_routed_with_flags(RecvFlags::DONT_WAIT) {
                Ok(received) => {
                    let reply = Message::copy_from(common::message_payload(received.parts()))
                        .expect("reply");
                    received.reply(vec![reply]).expect("reply");
                }
                Err(err) if err.code() == RecvResult::NoData => break,
                Err(err) => panic!("spot reqrep server recv_routed drain failed: {err}"),
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
    let mut ready_count = 0usize;
    let mut start_sender = None::<TcpStream>;
    let handshake_deadline = Instant::now() + ready_timeout + ready_settle + control_settle;
    while Instant::now() < handshake_deadline {
        let remaining = handshake_deadline.saturating_duration_since(Instant::now());
        match event_rx.recv_timeout(remaining) {
            Ok(ServerEvent::Stop) => return,
            Ok(ServerEvent::RunnerStart) => runner_start = true,
            Ok(ServerEvent::ClientConnected) => client_connected = true,
            Ok(ServerEvent::ReadyCount(count)) => ready_count += count,
            Ok(ServerEvent::StartSender(stream)) => start_sender = Some(stream),
            Err(mpsc::RecvTimeoutError::Timeout) => break,
            Err(mpsc::RecvTimeoutError::Disconnected) => break,
        }
        if runner_start
            && client_connected
            && ready_count >= settings.clients
            && start_sender.is_some()
        {
            break;
        }
    }
    if !runner_start
        || !client_connected
        || ready_count < settings.clients
        || start_sender.is_none()
    {
        panic!("spot reqrep server handshake timeout");
    }

    {
        let stream = start_sender.as_mut().expect("start sender");
        writeln!(stream, "START,{}", args.msg_size).expect("write start");
        stream.flush().expect("flush start");
    }

    while let Ok(event) = event_rx.recv() {
        if matches!(event, ServerEvent::Stop) {
            break;
        }
    }
}
