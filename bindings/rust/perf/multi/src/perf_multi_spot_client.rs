#[path = "perf_common.rs"]
mod common;

use std::io::{self, BufRead, BufReader, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::{
    Arc, Mutex,
    atomic::{AtomicBool, Ordering},
    mpsc,
};
use std::thread;
use std::time::{Duration, Instant};

use zlink::*;

const TOPIC: &str = "bench.topic";

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
    let (data_endpoint, control_endpoint) = args
        .endpoint
        .split_once(',')
        .expect("spot client expects data,control endpoint");

    let ready_settle = Duration::from_millis(env_u64("PERF_MULTI_SPOT_READY_SETTLE_MS", 1000));
    let control_settle = Duration::from_millis(env_u64("PERF_MULTI_SPOT_CONTROL_SETTLE_MS", 25));
    let ready_timeout = common::resolve_multi_connect_ready_timeout();
    let active_collect = Arc::new(AtomicBool::new(false));
    let latency = Arc::new(Mutex::new(common::LatencyStats::new()));
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

    let ctx = common::perf_client_context();
    let mut spots: Vec<Box<Spot>> = Vec::with_capacity(settings.clients);
    let mut nodes: Vec<SpotNode> = Vec::with_capacity(settings.clients);

    for _ in 0..settings.clients {
        let node = SpotNode::new(&ctx).expect("spot node");
        if matches!(args.transport.as_str(), "tls" | "wss") {
            let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
            let pem = common::load_tls_pem(&tls);
            node.set_tls_client(&pem.ca, "localhost", false)
                .expect("spot tls");
        }
        node.connect_peer(data_endpoint).expect("connect peer");
        let mut spot = Box::new(node.create_spot().expect("spot"));
        spot.set_subscription(TOPIC).expect("subscription");
        let spot_ptr = (&*spot as *const Spot) as usize;
        let active_collect = Arc::clone(&active_collect);
        let latency = Arc::clone(&latency);
        spot.on_dispatch_event(move |event| {
            if event != SpotDispatchEvent::SubscribeReadable {
                return;
            }
            let spot = unsafe { &*(spot_ptr as *const Spot) };
            loop {
                match spot.subscribe_with_flags(RecvFlags::DONT_WAIT) {
                    Ok(received) => {
                        let data = common::message_payload(received.parts());
                        if active_collect.load(Ordering::Acquire)
                            && common::is_valid_active_message(data, args.msg_size)
                        {
                            let sent_ts_ns = common::decode_sent_ts_ns(data);
                            latency.lock().expect("latency lock").record_ns(
                                common::now_ns().saturating_sub(sent_ts_ns.max(0) as u64) as f64,
                            );
                        }
                    }
                    Err(err) if err.code() == RecvResult::NoData => break,
                    Err(err) => panic!("spot client subscribe drain failed: {err}"),
                }
            }
        })
        .expect("dispatch handler");
        nodes.push(node);
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
        panic!("spot client control connection timeout");
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
        panic!("spot client start handshake timeout");
    }

    active_collect.store(true, Ordering::Release);
    thread::sleep(Duration::from_secs(settings.duration_seconds));
    active_collect.store(false, Ordering::Release);

    let stats = latency.lock().expect("latency lock").finish();
    common::print_result(
        "MULTI_SPOT",
        &args.transport,
        args.msg_size,
        settings.duration_seconds,
        &stats,
    );
}
