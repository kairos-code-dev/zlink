#[path = "perf_common.rs"]
mod common;

use std::future::Future;
use std::io::{self, BufRead, BufReader, Write};
use std::net::{TcpListener, TcpStream};
use std::pin::pin;
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc, Mutex,
};
use std::task::{Context as TaskContext, Poll, RawWaker, RawWakerVTable, Waker};
use std::thread;
use std::time::{Duration, Instant};

use zlink::*;

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

fn hex_value(ch: u8) -> u8 {
    match ch {
        b'0'..=b'9' => ch - b'0',
        b'a'..=b'f' => ch - b'a' + 10,
        b'A'..=b'F' => ch - b'A' + 10,
        _ => panic!("invalid hex digit"),
    }
}

fn decode_routing_id(hex: &str) -> RoutingId {
    assert!(hex.len() % 2 == 0, "routing id hex length must be even");
    let mut bytes = Vec::with_capacity(hex.len() / 2);
    for pair in hex.as_bytes().chunks(2) {
        bytes.push((hex_value(pair[0]) << 4) | hex_value(pair[1]));
    }
    RoutingId::from_bytes(&bytes)
}

fn noop_waker() -> Waker {
    unsafe fn clone(_: *const ()) -> RawWaker {
        RawWaker::new(std::ptr::null(), &VTABLE)
    }
    unsafe fn wake(_: *const ()) {}
    unsafe fn wake_by_ref(_: *const ()) {}
    unsafe fn drop(_: *const ()) {}

    static VTABLE: RawWakerVTable = RawWakerVTable::new(clone, wake, wake_by_ref, drop);
    let raw = RawWaker::new(std::ptr::null(), &VTABLE);
    unsafe { Waker::from_raw(raw) }
}

fn block_on<F: Future>(future: F) -> F::Output {
    let waker = noop_waker();
    let mut context = TaskContext::from_waker(&waker);
    let mut future = pin!(future);
    loop {
        match future.as_mut().poll(&mut context) {
            Poll::Ready(value) => return value,
            Poll::Pending => thread::yield_now(),
        }
    }
}

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();
    let mut endpoint_parts = args.endpoint.splitn(4, ',');
    let data_endpoint = endpoint_parts.next().expect("data endpoint");
    let control_endpoint = endpoint_parts.next().expect("control endpoint");
    let node_rid_hex = endpoint_parts.next().expect("node rid");
    let spot_rid_hex = endpoint_parts.next().expect("spot rid");

    let ready_settle = Duration::from_millis(env_u64("PERF_MULTI_SPOT_READY_SETTLE_MS", 1000));
    let control_settle = Duration::from_millis(env_u64("PERF_MULTI_SPOT_CONTROL_SETTLE_MS", 25));
    let request_timeout = Duration::from_millis(settings.recv_timeout_ms.max(settings.send_timeout_ms));

    let runner_connected = Arc::new(AtomicBool::new(false));
    let runner_start = Arc::new(AtomicBool::new(false));
    let started = Arc::new(AtomicBool::new(false));

    let (listener, client_control_endpoint) = control_listener();
    println!("CLIENT_CONTROL_ENDPOINT,{client_control_endpoint}");
    io::stdout().flush().ok();

    let ready_sender = Arc::new(Mutex::new(None::<TcpStream>));
    {
        let ready_sender = Arc::clone(&ready_sender);
        thread::spawn(move || {
            let (stream, _) = listener.accept().expect("accept server control");
            stream.set_nodelay(true).ok();
            ready_sender
                .lock()
                .expect("ready sender lock")
                .replace(stream);
        });
    }

    {
        let runner_connected = Arc::clone(&runner_connected);
        let runner_start = Arc::clone(&runner_start);
        thread::spawn(move || {
            let stdin = io::stdin();
            for line in stdin.lock().lines() {
                let line = line.unwrap_or_default();
                let text = line.trim();
                if text.starts_with("CONTROL_CONNECTED,") {
                    runner_connected.store(true, Ordering::Release);
                } else if text == format!("START,{}", args.msg_size) {
                    runner_start.store(true, Ordering::Release);
                } else if matches!(text, "STOP" | "QUIT") {
                    return;
                }
            }
        });
    }

    {
        let started = Arc::clone(&started);
        let control_endpoint = control_endpoint.to_string();
        thread::spawn(move || {
            let stream = TcpStream::connect(tcp_addr(&control_endpoint)).expect("connect control");
            let reader = BufReader::new(stream);
            for line in reader.lines() {
                let line = line.unwrap_or_default();
                if line.trim() == format!("START,{}", args.msg_size) {
                    started.store(true, Ordering::Release);
                    return;
                }
            }
        });
    }

    let node_rid = decode_routing_id(node_rid_hex);
    let spot_rid = decode_routing_id(spot_rid_hex);
    let ctx = common::perf_client_context();
    let latency = Arc::new(Mutex::new(common::LatencyStats::new()));
    let mut routers = Vec::with_capacity(settings.clients);
    for index in 0..settings.clients {
        let router = ctx.router_socket().expect("router");
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
        routers.push(router);
    }

    let ready_deadline = Instant::now() + Duration::from_secs(15);
    while Instant::now() < ready_deadline {
        if runner_connected.load(Ordering::Acquire)
            && ready_sender.lock().expect("ready sender lock").is_some()
        {
            break;
        }
        thread::yield_now();
    }
    if !runner_connected.load(Ordering::Acquire)
        || ready_sender.lock().expect("ready sender lock").is_none()
    {
        panic!("spot reqrep client control connection timeout");
    }

    thread::sleep(ready_settle);
    thread::sleep(control_settle);
    {
        let mut guard = ready_sender.lock().expect("ready sender lock");
        let stream = guard.as_mut().expect("ready sender");
        writeln!(stream, "CONNECTED").expect("write connected");
        writeln!(stream, "READY_COUNT,{},{}", args.msg_size, settings.clients)
            .expect("write ready count");
        stream.flush().expect("flush ready count");
    }

    println!("CLIENT_READY,{}", args.msg_size);
    io::stdout().flush().ok();

    let start_deadline = Instant::now() + Duration::from_secs(15);
    while Instant::now() < start_deadline {
        if runner_start.load(Ordering::Acquire) && started.load(Ordering::Acquire) {
            break;
        }
        thread::yield_now();
    }
    if !runner_start.load(Ordering::Acquire) || !started.load(Ordering::Acquire) {
        panic!("spot reqrep client start handshake timeout");
    }

    let mut workers = Vec::with_capacity(routers.len());
    for router in routers {
        let latency = Arc::clone(&latency);
        let node_rid = node_rid.clone();
        let spot_rid = spot_rid.clone();
        let msg_size = args.msg_size;
        let duration_seconds = settings.duration_seconds;
        workers.push(thread::spawn(move || {
            let deadline = Instant::now() + Duration::from_secs(duration_seconds);
            let mut seq = 1u64;
            let mut payload = vec![0u8; msg_size.max(common::HEADER_SIZE)];
            while Instant::now() < deadline {
                common::encode_header(&mut payload, common::PHASE_ACTIVE, msg_size as u32, seq);
                match block_on(router.request_to_spot(
                    node_rid.clone(),
                    spot_rid.clone(),
                    vec![Message::copy_from(&payload).expect("request")],
                    request_timeout,
                )) {
                    Ok(reply) => {
                        let data = reply.first().map(|message| message.as_bytes()).unwrap_or(&[]);
                        if !common::is_valid_active_message(data, msg_size) {
                            continue;
                        }
                        let sent_ts_ns = common::decode_sent_ts_ns(data);
                        let latency_ns =
                            common::now_ns().saturating_sub(sent_ts_ns.max(0) as u64) as f64 / 2.0;
                        latency.lock().expect("latency lock").record_ns(latency_ns);
                        seq += 1;
                    }
                    Err(err) if err.internal_errno() == libc::EAGAIN => {}
                    Err(err) => panic!("spot reqrep request failed: {err}"),
                }
            }
        }));
    }
    for worker in workers {
        worker.join().expect("worker join");
    }

    let stats = latency.lock().expect("latency lock").finish();
    common::print_result(
        "MULTI_SPOT_REQREP",
        &args.transport,
        args.msg_size,
        settings.duration_seconds,
        &stats,
    );
}
