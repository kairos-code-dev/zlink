//! Single SPOT_REQREP throughput/latency benchmark.

mod common;

use std::future::Future;
use std::pin::pin;
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc,
};
use std::task::{Context as TaskContext, Poll, RawWaker, RawWakerVTable, Waker};
use std::thread;
use std::time::Duration;

use zlink::*;

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
    let replier_node_rid = replier_node.routing_id().expect("replier node rid");
    let replier_spot_rid = replier.routing_id().expect("replier spot rid");

    let responder_ready = Arc::new(AtomicBool::new(false));
    let spot_ptr = (&replier as *const Spot) as usize;
    replier
        .on_dispatch_event({
            let responder_ready = Arc::clone(&responder_ready);
            move |event| {
                if event != SpotDispatchEvent::RoutedReadable {
                    return;
                }

                let replier = unsafe { &*(spot_ptr as *const Spot) };
                loop {
                    match replier.recv_routed_with_flags(RecvFlags::DONT_WAIT) {
                        Ok(received) => {
                            let data = common::message_payload(received.parts());
                            if common::is_valid_message(data, config.size) {
                                responder_ready.store(true, Ordering::Release);
                                let reply = Message::copy_from(data).expect("reply message");
                                received.reply(vec![reply]).expect("reply");
                            }
                        }
                        Err(err) if err.code() == RecvResult::NoData => break,
                        Err(err) => panic!("spot reqrep recv_routed drain failed: {err}"),
                    }
                }
            }
        })
        .expect("on dispatch event");

    let probe_timeout = common::resolve_single_ready_timeout();
    let mut probe = vec![0u8; config.size.max(common::HEADER_SIZE)];
    common::encode_header(&mut probe, common::PHASE_WARMUP, config.size as u32, 0);
    let ready_reply = block_on(requester.request_to_spot(
            replier_node_rid.clone(),
            replier_spot_rid.clone(),
            vec![Message::copy_from(&probe).expect("probe")],
            probe_timeout,
        ))
        .expect("ready probe");
    let ready_data = ready_reply
        .first()
        .map(|message| message.as_bytes())
        .unwrap_or(&[]);
    assert!(common::is_valid_message(ready_data, config.size));
    assert!(responder_ready.load(Ordering::Acquire));

    thread::sleep(common::resolve_single_spot_ready_settle());

    let collector = common::MetricCollector::new();
    let stats = collector.shared();
    let active_end = std::time::Instant::now() + Duration::from_secs(config.duration_seconds);
    let mut seq: u64 = 1;
    let mut payload = vec![0u8; config.size.max(common::HEADER_SIZE)];
    while std::time::Instant::now() < active_end {
        common::encode_header(&mut payload, common::PHASE_ACTIVE, config.size as u32, seq);
        let reply = block_on(requester.request_to_spot(
                replier_node_rid.clone(),
                replier_spot_rid.clone(),
                vec![Message::copy_from(&payload).expect("request")],
                probe_timeout,
            ))
            .expect("active request");
        let data = reply.first().map(|message| message.as_bytes()).unwrap_or(&[]);
        if common::is_valid_active_message(data, config.size) {
            let sent_ts_ns = common::decode_sent_ts_ns(data);
            let latency_ns = (common::now_ns() as i64)
                .saturating_sub(sent_ts_ns)
                .max(0) as u64
                / 2;
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
