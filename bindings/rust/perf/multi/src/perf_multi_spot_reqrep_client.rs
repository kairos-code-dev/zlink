#[path = "perf_common.rs"]
mod common;

use std::io::{self, BufRead, Write};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{mpsc, Arc};
use std::thread;
use std::time::{Duration, Instant};

use zlink::*;
use zlink::poller::POLLCOMPLETION;

const PATTERN: &str = "MULTI_SPOT_REQREP";
const TOPIC: &str = "bench";
const SERVER_NODE_RID: &[u8] = b"SPOT-REQREP-SERVER-NODE";
const SERVER_SPOT_RID: &[u8] = b"SPOT-REQREP-SERVER-SPOT";

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
        .submit(move |result| {
            let _ = tx.send(result);
        });
    match submit.and_then(|_| {
        rx.recv()
            .map_err(|_| SubmitError::new(SubmitResult::InternalError, libc::EINVAL))?
            .map_err(|err| SubmitError::new(SubmitResult::InternalError, err.internal_errno()))
    }) {
        Ok(parts) => Some(parts),
        Err(_) => None,
    }
}

fn request_spot_reply_with_poller(
    spot: &Spot,
    node_rid: RoutingId,
    spot_rid: RoutingId,
    msg: Message,
    timeout: Duration,
    poller: &Poller,
    events: &mut [PollEvent],
) -> Option<Vec<Message>> {
    let (tx, rx) = mpsc::channel();
    let submit = spot
        .request_to_spot(node_rid, spot_rid)
        .message(msg)
        .timeout(timeout)
        .submit(move |result| {
            let _ = tx.send(result);
        });
    if submit.is_err() {
        return None;
    }
    let deadline = Instant::now() + timeout + Duration::from_millis(100);
    while Instant::now() < deadline {
        match rx.try_recv() {
            Ok(Ok(parts)) => return Some(parts),
            Ok(Err(_)) => return None,
            Err(mpsc::TryRecvError::Disconnected) => return None,
            Err(mpsc::TryRecvError::Empty) => {}
        }
        let remaining = deadline.saturating_duration_since(Instant::now());
        let wait_ms = remaining.min(Duration::from_millis(50)).as_millis() as i64;
        let _ = poller.wait(events, wait_ms.max(1));
    }
    None
}

fn active_spot_reqrep_slot_limit(total_slots: usize, msg_size: usize) -> usize {
    if msg_size >= 131072 {
        return total_slots.min(8);
    }
    if msg_size >= 65536 {
        return total_slots.min(32);
    }
    total_slots
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
    let (data_endpoint, control_endpoint) = args
        .endpoint
        .split_once(',')
        .expect("spot reqrep client expects data,control endpoint");
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
    setup_tls_server(&data_node, &args.transport);
    setup_tls_client(&data_node, &args.transport);
    setup_tls_server(&control_node, &args.transport);
    setup_tls_client(&control_node, &args.transport);
    common::apply_multi_spot_node_admission(&data_node, &settings);
    common::apply_multi_spot_node_admission(&control_node, &settings);
    data_node
        .set_routing_id(&RoutingId::from_bytes(b"SPOT-REQREP-CLIENT-NODE"))
        .expect("data rid");

    let control_pub = control_node.create_spot().expect("control pub");
    let control_sub = control_node.create_spot().expect("control sub");
    control_sub.set_subscription(TOPIC).expect("control sub");
    let Some(control_bind) =
        common::benchmark_endpoint(PATTERN, &args.transport, "multi-spot-reqrep-control-client")
    else {
        return;
    };
    control_node.bind(&control_bind).expect("control bind");
    control_node
        .connect_peer(&control_endpoint)
        .expect("connect server control");
    let client_control_endpoint = control_node.last_endpoint().unwrap_or(control_bind);
    println!("CLIENT_CONTROL_ENDPOINT,{client_control_endpoint}");
    io::stdout().flush().ok();

    let Some(data_bind) =
        common::benchmark_endpoint(PATTERN, &args.transport, "multi-spot-reqrep-client")
    else {
        return;
    };
    data_node.bind(&data_bind).expect("client data bind");
    let data_endpoint_local = data_node.last_endpoint().unwrap_or(data_bind);
    data_node
        .connect_peer(&data_endpoint)
        .expect("connect data");

    let server_node_rid = RoutingId::from_bytes(SERVER_NODE_RID);
    let server_spot_rid = RoutingId::from_bytes(SERVER_SPOT_RID);
    let mut latency = common::LatencyStats::new();
    let mut spots: Vec<Box<Spot>> = Vec::with_capacity(settings.clients);
    let mut payloads = Vec::with_capacity(settings.clients);
    let mut seqs = vec![1u64; settings.clients];
    let mut waiting = Vec::with_capacity(settings.clients);
    for index in 0..settings.clients {
        let spot = Box::new(data_node.create_spot().expect("spot"));
        spot.set_routing_id(&RoutingId::from_bytes(
            format!("SPOT-REQREP-CLIENT-SPOT-{index}").as_bytes(),
        ))
        .expect("spot rid");
        payloads.push(vec![0u8; args.msg_size.max(common::HEADER_SIZE)]);
        waiting.push(Arc::new(AtomicBool::new(false)));
        spots.push(spot);
    }
    let poller = Poller::new().expect("spot reqrep completion poller");
    let mut poll_events = vec![PollEvent::default(); settings.clients.max(1)];
    for (index, spot) in spots.iter().enumerate() {
        poller
            .add_socket(spot.as_ref(), POLLCOMPLETION, index)
            .expect("spot reqrep completion poller add");
    }

    if !matches!(
        event_rx.recv_timeout(ready_timeout),
        Ok(ClientEvent::RunnerControlConnected)
    ) {
        panic!("spot reqrep client control connection timeout");
    }
    thread::sleep(ready_settle);
    thread::sleep(control_settle);
    if !publish_control(
        &control_pub,
        &format!("DATA_ENDPOINT,{data_endpoint_local}"),
        ready_timeout,
    ) {
        panic!("spot reqrep data endpoint publish timeout");
    }
    thread::sleep(control_settle);
    let _ = publish_control(&control_pub, "CONNECTED", ready_timeout);

    let mut probe = vec![0u8; args.msg_size.max(common::HEADER_SIZE)];
    common::encode_header(&mut probe, common::PHASE_WARMUP, args.msg_size as u32, 0);
    let probe_deadline = Instant::now() + ready_timeout;
    while Instant::now() < probe_deadline {
        if request_spot_reply_with_poller(
            &spots[0],
            server_node_rid.clone(),
            server_spot_rid.clone(),
            Message::copy_from(&probe).expect("probe"),
            Duration::from_millis(settings.recv_timeout_ms.max(settings.send_timeout_ms)),
            &poller,
            &mut poll_events,
        )
        .is_some()
        {
            break;
        }
        thread::sleep(Duration::from_millis(10));
    }
    if Instant::now() >= probe_deadline {
        panic!("spot reqrep probe-ready timeout");
    }

    if !publish_control(
        &control_pub,
        &format!("READY_COUNT,{},{}", args.msg_size, settings.clients),
        ready_timeout,
    ) {
        panic!("spot reqrep ready publish timeout");
    }
    println!("CLIENT_READY,{}", args.msg_size);
    io::stdout().flush().ok();

    if !matches!(
        event_rx.recv_timeout(ready_timeout),
        Ok(ClientEvent::RunnerStart)
    ) {
        panic!("spot reqrep runner start handshake timeout");
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
        panic!("spot reqrep direct start handshake timeout");
    }

    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds);
    let (latency_tx, latency_rx) = mpsc::channel::<f64>();
    let active_slots = active_spot_reqrep_slot_limit(spots.len(), args.msg_size);
    let msg_size = args.msg_size;
    while Instant::now() < deadline {
        let mut submitted = false;
        for index in 0..active_slots {
            if waiting[index].load(Ordering::Acquire) {
                continue;
            }
            common::encode_header(
                &mut payloads[index],
                common::PHASE_ACTIVE,
                args.msg_size as u32,
                seqs[index],
            );
            let request = Message::copy_from(&payloads[index]).expect("request");
            let waiting_flag = Arc::clone(&waiting[index]);
            let tx = latency_tx.clone();
            waiting_flag.store(true, Ordering::Release);
            let submit = spots[index]
                .request_to_spot(server_node_rid.clone(), server_spot_rid.clone())
                .message(request)
                .timeout(Duration::from_millis(
                    settings.recv_timeout_ms.max(settings.send_timeout_ms),
                ))
                .submit(move |result| {
                    waiting_flag.store(false, Ordering::Release);
                    let Ok(reply) = result else {
                        return;
                    };
                    let data = common::message_payload(&reply);
                    if Instant::now() <= deadline
                        && common::is_valid_active_message(data, msg_size)
                    {
                        let sent_ts_ns = common::decode_sent_ts_ns(data);
                        let now_ns = common::now_ns();
                        if sent_ts_ns > 0 && now_ns >= sent_ts_ns as u64 {
                            let _ = tx.send((now_ns - sent_ts_ns as u64) as f64 / 2.0);
                        }
                    }
                });
            match submit {
                Ok(()) => {
                    seqs[index] += 1;
                    submitted = true;
                }
                Err(_) => {
                    waiting[index].store(false, Ordering::Release);
                }
            }
        }
        while let Ok(value) = latency_rx.try_recv() {
            latency.record_ns(value);
        }
        if !submitted {
            let remaining = deadline.saturating_duration_since(Instant::now());
            let wait_ms = remaining.min(Duration::from_millis(50)).as_millis() as i64;
            if wait_ms <= 0 {
                break;
            }
            let _ = poller.wait(&mut poll_events, wait_ms);
        }
    }
    while let Ok(value) = latency_rx.try_recv() {
        latency.record_ns(value);
    }

    common::print_result(
        PATTERN,
        &args.transport,
        args.msg_size,
        settings.duration_seconds,
        &latency.finish(),
    );
}
