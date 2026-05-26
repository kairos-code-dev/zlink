#[path = "perf_common.rs"]
mod common;

use std::io::{self, BufRead, Write};
use std::sync::mpsc;
use std::thread;
use std::time::{Duration, Instant};

use zlink::*;

const PATTERN: &str = "MULTI_SPOT_SENDSEND";
const TOPIC: &str = "bench";
const SERVER_NODE_RID: &[u8] = b"SPOT-SENDSEND-SERVER-NODE";
const SERVER_SPOT_RID: &[u8] = b"SPOT-SENDSEND-SERVER-SPOT";

enum ClientEvent {
    RunnerControlConnected,
    RunnerStart,
    Stop,
}

struct Slot {
    spot: Box<Spot>,
    payload: Vec<u8>,
    seq: u64,
    waiting_reply: bool,
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
                common::poll_idle_until(deadline, Duration::from_millis(10));
            }
            Err(err) => panic!("control publish failed: {err}"),
        }
    }
    false
}

fn send_payload(spot: &Spot, node_rid: &RoutingId, spot_rid: &RoutingId, payload: &[u8]) -> bool {
    let message = Message::copy_from(payload).expect("payload message");
    match spot
        .send_to_spot(node_rid.clone(), spot_rid.clone())
        .message(message)
        .flags(SendFlags::DONT_WAIT)
        .submit()
    {
        Ok(sent) => sent,
        Err(_) => false,
    }
}

fn active_spot_slot_limit(total: usize, msg_size: usize) -> usize {
    if msg_size >= 131_072 {
        total.min(8)
    } else if msg_size >= 65_536 {
        total.min(24)
    } else {
        total
    }
}

fn drain_slot(
    slot: &mut Slot,
    expected_size: usize,
    deadline: Instant,
    latency: &mut common::LatencyStats,
    record: bool,
) -> bool {
    let mut progressed = false;
    loop {
        let mut received = Received::empty();
        match slot.spot.recv_routed(&mut received, RecvFlags::DONT_WAIT) {
            Ok(true) => {
                progressed = true;
                slot.waiting_reply = false;
                if received.request_seq().unwrap_or(0) != 0 {
                    continue;
                }
                let data = common::message_payload(received.parts());
                if record
                    && Instant::now() <= deadline
                    && common::is_valid_active_message(data, expected_size)
                {
                    let sent_ts_ns = common::decode_sent_ts_ns(data);
                    let now_ns = common::now_ns();
                    if sent_ts_ns > 0 && now_ns >= sent_ts_ns as u64 {
                        latency.record_ns((now_ns - sent_ts_ns as u64) as f64 / 2.0);
                    }
                }
            }
            Ok(false) => break,
            Err(err) if err.code() == RecvResult::NoData => break,
            Err(_) => break,
        }
    }
    progressed
}

fn handle_poll_events(
    events: &[PollEvent],
    slots: &mut [Slot],
    expected_size: usize,
    deadline: Instant,
    latency: &mut common::LatencyStats,
    record: bool,
) -> bool {
    let mut progressed = false;
    for event in events {
        let index = event.slot;
        if index >= slots.len() || event.revents & POLLIN == 0 {
            continue;
        }
        progressed |= drain_slot(&mut slots[index], expected_size, deadline, latency, record);
    }
    progressed
}

fn any_waiting_reply(slots: &[Slot], active_slots: usize) -> bool {
    slots
        .iter()
        .take(active_slots.min(slots.len()))
        .any(|slot| slot.waiting_reply)
}

fn poll_timeout_until(deadline: Instant, max_wait: Duration) -> i64 {
    let remaining = deadline.saturating_duration_since(Instant::now());
    if remaining.is_zero() {
        return 0;
    }
    remaining.min(max_wait).as_millis().max(1) as i64
}

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();
    let (data_endpoint, control_endpoint) = args
        .endpoint
        .split_once(',')
        .expect("spot sendsend client expects data,control endpoint");
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
        .set_routing_id(&RoutingId::from_bytes(b"SPOT-SENDSEND-CLIENT-NODE"))
        .expect("data rid");

    let control_pub = control_node.create_spot().expect("control pub");
    let control_sub = control_node.create_spot().expect("control sub");
    control_sub.set_subscription(TOPIC).expect("control sub");
    let Some(control_bind) = common::benchmark_endpoint(
        PATTERN,
        &args.transport,
        "multi-spot-sendsend-control-client",
    ) else {
        return;
    };
    control_node
        .set_pub_bind(&control_bind)
        .expect("control bind");
    control_node
        .connect_peer(&control_endpoint)
        .expect("connect server control");
    let client_control_endpoint = control_node.last_endpoint().unwrap_or(control_bind);
    println!("CLIENT_CONTROL_ENDPOINT,{client_control_endpoint}");
    io::stdout().flush().ok();

    let Some(data_bind) =
        common::benchmark_endpoint(PATTERN, &args.transport, "multi-spot-sendsend-client")
    else {
        return;
    };
    let Some(data_router_bind) = common::benchmark_endpoint(
        PATTERN,
        &args.transport,
        "multi-spot-sendsend-router-client",
    ) else {
        return;
    };
    data_node
        .set_router_bind(&data_router_bind)
        .expect("client data router bind endpoint");
    data_node
        .set_pub_bind(&data_bind)
        .expect("client data bind");
    let data_endpoint_local = data_node.last_endpoint().unwrap_or(data_bind);
    data_node
        .connect_peer(&data_endpoint)
        .expect("connect data");

    let server_node_rid = RoutingId::from_bytes(SERVER_NODE_RID);
    let server_spot_rid = RoutingId::from_bytes(SERVER_SPOT_RID);
    let mut slots = Vec::with_capacity(settings.clients);
    for index in 0..settings.clients {
        let spot = Box::new(data_node.create_spot().expect("spot"));
        spot.set_routing_id(&RoutingId::from_bytes(
            format!("SPOT-SENDSEND-{index}").as_bytes(),
        ))
        .expect("spot rid");
        slots.push(Slot {
            spot,
            payload: vec![0u8; args.msg_size.max(common::HEADER_SIZE)],
            seq: 1,
            waiting_reply: false,
        });
    }
    let probe_poller = Poller::new().expect("spot sendsend probe poller");
    probe_poller
        .add_socket(&*slots[0].spot, POLLIN, 0)
        .expect("spot sendsend probe poller add");
    let mut probe_events = vec![PollEvent::default(); 1];

    if !matches!(
        event_rx.recv_timeout(ready_timeout),
        Ok(ClientEvent::RunnerControlConnected)
    ) {
        panic!("spot sendsend client control connection timeout");
    }
    thread::sleep(ready_settle);
    thread::sleep(control_settle);
    if !publish_control(
        &control_pub,
        &format!("DATA_ENDPOINT,{data_endpoint_local}"),
        ready_timeout,
    ) {
        panic!("spot sendsend data endpoint publish timeout");
    }
    thread::sleep(control_settle);
    let _ = publish_control(&control_pub, "CONNECTED", ready_timeout);

    let mut probe_payload = vec![0u8; args.msg_size.max(common::HEADER_SIZE)];
    common::encode_header(
        &mut probe_payload,
        common::PHASE_WARMUP,
        args.msg_size as u32,
        0,
    );
    let probe_deadline = Instant::now() + ready_timeout;
    while Instant::now() < probe_deadline {
        if !slots[0].waiting_reply
            && send_payload(
                &slots[0].spot,
                &server_node_rid,
                &server_spot_rid,
                &probe_payload,
            )
        {
            slots[0].waiting_reply = true;
        }
        if drain_slot(
            &mut slots[0],
            args.msg_size,
            probe_deadline,
            &mut common::LatencyStats::new(),
            false,
        ) {
            break;
        }
        common::wait_control_readable_until(&probe_poller, &mut probe_events, probe_deadline);
    }
    if Instant::now() >= probe_deadline {
        panic!("spot sendsend probe-ready timeout");
    }

    if !publish_control(
        &control_pub,
        &format!("READY_COUNT,{},{}", args.msg_size, settings.clients),
        ready_timeout,
    ) {
        panic!("spot sendsend ready publish timeout");
    }
    println!("CLIENT_READY,{}", args.msg_size);
    io::stdout().flush().ok();

    if !matches!(
        event_rx.recv_timeout(ready_timeout),
        Ok(ClientEvent::RunnerStart)
    ) {
        panic!("spot sendsend runner start handshake timeout");
    }
    let control_read_poller = common::control_read_poller(&control_sub);
    let mut control_events = vec![PollEvent::default(); 1];
    let direct_deadline = Instant::now() + ready_timeout;
    let mut direct_started = false;
    while Instant::now() < direct_deadline {
        if let Some(payload) = control_payload(&control_sub) {
            if payload == format!("START,{}", args.msg_size) {
                direct_started = true;
                break;
            }
        }
        common::wait_control_readable_until(
            &control_read_poller,
            &mut control_events,
            direct_deadline,
        );
    }
    if !direct_started {
        panic!("spot sendsend direct start handshake timeout");
    }

    let mut latency = common::LatencyStats::new();
    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds);
    let active_slots = active_spot_slot_limit(slots.len(), args.msg_size);
    let poller = Poller::new().expect("spot sendsend poller");
    for (index, slot) in slots.iter().enumerate() {
        poller
            .add_socket(&*slot.spot, POLLIN, index)
            .expect("spot sendsend poller add");
    }
    let mut poll_events = vec![PollEvent::default(); slots.len().max(1)];
    while Instant::now() < deadline {
        let mut progressed = false;
        for slot in slots.iter_mut().take(active_slots) {
            if slot.waiting_reply {
                progressed |= drain_slot(slot, args.msg_size, deadline, &mut latency, true);
                continue;
            }
            common::encode_header(
                &mut slot.payload,
                common::PHASE_ACTIVE,
                args.msg_size as u32,
                slot.seq,
            );
            if send_payload(
                &slot.spot,
                &server_node_rid,
                &server_spot_rid,
                &slot.payload,
            ) {
                slot.waiting_reply = true;
                slot.seq += 1;
                progressed = true;
            } else {
                progressed |= drain_slot(slot, args.msg_size, deadline, &mut latency, true);
            }
        }
        if !progressed {
            let wait_ms = poll_timeout_until(deadline, Duration::from_millis(50));
            if wait_ms <= 0 {
                break;
            }
            match poller.wait(&mut poll_events, wait_ms) {
                Ok(event_count) => {
                    handle_poll_events(
                        &poll_events[..event_count],
                        &mut slots,
                        args.msg_size,
                        deadline,
                        &mut latency,
                        true,
                    );
                }
                Err(err) => panic!("spot sendsend poller wait failed: {err}"),
            }
        }
    }

    let pending_deadline = Instant::now() + Duration::from_secs(1);
    while any_waiting_reply(&slots, active_slots) && Instant::now() < pending_deadline {
        let wait_ms = poll_timeout_until(pending_deadline, Duration::from_millis(50));
        if wait_ms <= 0 {
            break;
        }
        match poller.wait(&mut poll_events, wait_ms) {
            Ok(event_count) => {
                handle_poll_events(
                    &poll_events[..event_count],
                    &mut slots,
                    args.msg_size,
                    deadline,
                    &mut latency,
                    true,
                );
            }
            Err(err) => panic!("spot sendsend pending poller wait failed: {err}"),
        }
    }

    common::print_result(
        PATTERN,
        &args.transport,
        args.msg_size,
        settings.duration_seconds,
        &latency.finish(),
    );
}
