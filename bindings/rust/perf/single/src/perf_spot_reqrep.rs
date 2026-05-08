//! Single SPOT_REQREP throughput/latency benchmark.

mod common;

use std::sync::{
    mpsc,
    Arc,
    atomic::{AtomicBool, Ordering},
};
use std::thread;
use std::time::{Duration, Instant};

use zlink::*;

const NODE_RID: &[u8] = b"perf-spot-reqrep-node";
const SPOT_RID: &[u8] = b"perf-spot-reqrep-spot";
const REQUESTER_RID: &[u8] = b"perf-spot-reqrep-requester";

fn request_spot_reply(
    requester: &RouterSocket,
    node_rid: RoutingId,
    spot_rid: RoutingId,
    msg: Message,
    timeout: Duration,
) -> Result<Vec<Message>, String> {
    let (tx, rx) = mpsc::channel();
    requester
        .request_to_spot_callback(
            node_rid,
            spot_rid,
            vec![msg],
            move |result| {
                let _ = tx.send(result);
            },
            SendFlags::NONE,
            timeout,
        )
        .map_err(|err| format!("spot request submit: {err}"))?;
    rx.recv_timeout(timeout + Duration::from_millis(100))
        .map_err(|err| format!("spot request callback: {err}"))?
        .map_err(|err| format!("spot request reply: {err}"))
}

fn wait_for_probe_reply(
    requester: &RouterSocket,
    node_rid: RoutingId,
    spot_rid: RoutingId,
    msg: Message,
    msg_size: usize,
) -> Vec<Message> {
    let deadline = Instant::now() + common::resolve_single_ready_timeout();
    let mut payload = Some(msg);
    loop {
        let Some(current_msg) = payload.take() else {
            panic!("probe payload missing");
        };
        let reply = match request_spot_reply(
            requester,
            node_rid.clone(),
            spot_rid.clone(),
            current_msg,
            Duration::from_millis(500),
        ) {
            Ok(reply) => reply,
            Err(err) => {
                if Instant::now() >= deadline {
                    panic!("{err}");
                }
                let mut next_probe = vec![0u8; msg_size.max(common::HEADER_SIZE)];
                common::encode_header(&mut next_probe, common::PHASE_WARMUP, msg_size as u32, 0);
                payload = Some(Message::copy_from(&next_probe).expect("probe retry"));
                thread::sleep(Duration::from_millis(1));
                continue;
            }
        };
        let data = reply
            .first()
            .map(|message| message.as_bytes())
            .unwrap_or(&[]);
        if common::is_valid_message(data, msg_size) {
            return reply;
        }
        if Instant::now() >= deadline {
            panic!("spot reqrep probe-ready timeout");
        }
        let mut next_probe = vec![0u8; msg_size.max(common::HEADER_SIZE)];
        common::encode_header(&mut next_probe, common::PHASE_WARMUP, msg_size as u32, 0);
        payload = Some(Message::copy_from(&next_probe).expect("probe retry"));
        thread::sleep(Duration::from_millis(1));
    }
}

fn main() {
    let config = common::PerfConfig::from_env_and_args();
    let Some(bind_endpoint) = common::resolve_endpoint_or_emit_unsupported(
        "SPOT_REQREP",
        &config.transport,
        "spot-reqrep",
    ) else {
        return;
    };

    let ctx = common::perf_context();
    let requester = ctx.router_socket().expect("requester");
    let replier_node = SpotNode::new(&ctx).expect("replier node");
    common::apply_single_spot_node_admission(&replier_node);
    requester
        .set_routing_id(&RoutingId::from_bytes(REQUESTER_RID))
        .expect("requester routing id");
    replier_node
        .set_routing_id(&RoutingId::from_bytes(NODE_RID))
        .expect("replier node routing id");
    if matches!(config.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        let pem = common::load_tls_pem(&tls);
        common::setup_raw_tls_client(&requester, &tls).expect("requester tls");
        replier_node
            .set_tls_server(&pem.cert, &pem.key, false)
            .expect("replier tls");
    }
    requester
        .common_options()
        .set_send_hwm(common::resolve_single_send_hwm())
        .expect("requester sndhwm");
    requester
        .common_options()
        .set_recv_hwm(common::resolve_single_recv_hwm())
        .expect("requester rcvhwm");

    let mut replier = replier_node.create_spot().expect("replier spot");
    replier
        .set_routing_id(&RoutingId::from_bytes(SPOT_RID))
        .expect("replier spot routing id");
    if let Err(err) = replier_node.bind(&bind_endpoint) {
        if common::handle_transport_setup_error("SPOT_REQREP", &config.transport, "bind", err) {
            return;
        }
        panic!("bind: {err}");
    }
    let endpoint = replier_node.last_endpoint().expect("endpoint");
    if let Err(err) = requester.connect(&endpoint) {
        if common::handle_transport_setup_error("SPOT_REQREP", &config.transport, "connect", err) {
            return;
        }
        panic!("connect: {err}");
    }
    thread::sleep(common::resolve_single_spot_ready_settle());
    let replier_node_rid = replier_node.routing_id().expect("replier node rid");
    let replier_spot_rid = replier.routing_id().expect("replier spot rid");

    let responder_ready = Arc::new(AtomicBool::new(false));
    let spot_ptr = (&replier as *const Spot) as usize;
    replier
        .on_dispatch_event({
            let responder_ready = Arc::clone(&responder_ready);
            move |info| {
                if info.event != SpotDispatchEvent::RoutedReadable {
                    return;
                }

                let replier = unsafe { &*(spot_ptr as *const Spot) };
                loop {
                    match replier.recv_routed_with_flags(RecvFlags::DONT_WAIT) {
                        Ok(Some(received)) => {
                            let data = common::message_payload(received.parts());
                            if common::is_valid_message(data, config.size) {
                                responder_ready.store(true, Ordering::Release);
                                let reply = Message::copy_from(data).expect("reply message");
                                received.reply(vec![reply]).expect("reply");
                            }
                        }
                        Ok(None) => break,
                        Err(err) => panic!("spot reqrep recv_routed drain failed: {err}"),
                    }
                }
            }
        })
        .expect("on dispatch event");

    let mut probe = vec![0u8; config.size.max(common::HEADER_SIZE)];
    common::encode_header(&mut probe, common::PHASE_WARMUP, config.size as u32, 0);
    let ready_reply = wait_for_probe_reply(
        &requester,
        replier_node_rid.clone(),
        replier_spot_rid.clone(),
        Message::copy_from(&probe).expect("probe"),
        config.size,
    );
    let ready_data = ready_reply
        .first()
        .map(|message| message.as_bytes())
        .unwrap_or(&[]);
    assert!(common::is_valid_message(ready_data, config.size));
    assert!(responder_ready.load(Ordering::Acquire));

    thread::sleep(common::resolve_single_spot_ready_settle());

    let collector = common::MetricCollector::new();
    let stats = collector.shared();
    let active_end = Instant::now() + Duration::from_secs(config.duration_seconds);
    let mut seq: u64 = 1;
    let mut payload = vec![0u8; config.size.max(common::HEADER_SIZE)];
    while Instant::now() < active_end {
        common::encode_header(&mut payload, common::PHASE_ACTIVE, config.size as u32, seq);
        let reply = request_spot_reply(
            &requester,
            replier_node_rid.clone(),
            replier_spot_rid.clone(),
            Message::copy_from(&payload).expect("request"),
            common::resolve_single_ready_timeout(),
        )
        .expect("spot request reply");
        let data = reply
            .first()
            .map(|message| message.as_bytes())
            .unwrap_or(&[]);
        if Instant::now() <= active_end && common::is_valid_active_message(data, config.size) {
            let sent_ts_ns = common::decode_sent_ts_ns(data);
            let latency_ns = (common::now_ns() as i64).saturating_sub(sent_ts_ns).max(0) as u64 / 2;
            stats.lock().unwrap().record_ns(latency_ns);
        }
        seq += 1;
    }

    let result = collector.finish();
    common::print_result(
        "SPOT_REQREP",
        &config.transport,
        config.size,
        config.duration_seconds,
        &result,
    );
}
