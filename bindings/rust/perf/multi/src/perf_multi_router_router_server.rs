#[path = "perf_common.rs"]
mod common;

use std::collections::VecDeque;
use std::io::{self, BufRead};
use std::sync::{
    Arc,
    atomic::{AtomicBool, Ordering},
};
use std::time::Duration;
use zlink::*;

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();
    let ctx = common::perf_server_context();
    let router = ctx.router_socket().expect("router");
    let rid = RoutingId::from_bytes(b"perf-rr-server");
    router.set_routing_id(&rid).expect("set rid");
    router
        .common_options()
        .set_send_hwm(settings.send_hwm)
        .expect("sndhwm");
    router
        .common_options()
        .set_recv_hwm(settings.recv_hwm)
        .expect("rcvhwm");
    if settings.msg_unit_bytes > 0 {
        router
            .common_options()
            .set_auto_hwm_msg_unit_bytes(settings.msg_unit_bytes)
            .expect("auto hwm msg unit");
    }
    router
        .common_options()
        .set_recv_timeout(Duration::from_millis(1))
        .expect("recv timeout");
    if matches!(args.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        common::setup_raw_tls_server(&router, &tls).expect("server tls");
    }
    let Some(bind_endpoint) =
        common::resolve_server_bind_endpoint("MULTI_ROUTER_ROUTER", &args.transport)
    else {
        return;
    };
    if let Err(err) = router.bind(&bind_endpoint) {
        if common::handle_transport_setup_error("MULTI_ROUTER_ROUTER", &args.transport, "bind", err)
        {
            return;
        }
        panic!("bind: {err}");
    }
    let endpoint = router.last_endpoint().expect("endpoint");
    common::print_ready(&endpoint);
    let mut pending = VecDeque::<(RoutingId, Vec<u8>)>::new();
    let stop = Arc::new(AtomicBool::new(false));
    let stop_reader = stop.clone();
    std::thread::spawn(move || {
        let stdin = io::stdin();
        for line in stdin.lock().lines() {
            let line = line.unwrap_or_default();
            if matches!(line.trim(), "STOP" | "QUIT") {
                stop_reader.store(true, Ordering::Release);
                break;
            }
        }
    });
    let mut received = zlink::Received::empty();
    while !stop.load(Ordering::Acquire) {
        let mut progressed = false;
        loop {
            match router.recv(&mut received, RecvFlags::DONT_WAIT) {
                Ok(true) => {
                    let Some(rid) = received.routing_id().cloned() else {
                        continue;
                    };
                    let reply_bytes = common::message_payload(received.parts()).to_vec();
                    if pending.is_empty() {
                        match received
                            .send()
                            .message(Message::copy_from(&reply_bytes).expect("reply"))
                            .flags(SendFlags::DONT_WAIT)
                            .submit()
                        {
                            Ok(true) => {
                                progressed = true;
                                continue;
                            }
                            Ok(false) => {}
                            Err(err) => panic!("received send failed: {err}"),
                        }
                    }
                    pending.push_back((rid, reply_bytes));
                    progressed = true;
                }
                Ok(false) => break,
                Err(err) => panic!("router recv failed: {err}"),
            }
        }
        while let Some((rid, reply_bytes)) = pending.pop_front() {
            match router
                .send(&rid)
                .message(Message::copy_from(&reply_bytes).expect("pending reply"))
                .flags(SendFlags::DONT_WAIT)
                .submit()
            {
                Ok(true) => progressed = true,
                Ok(false) => {
                    pending.push_front((rid, reply_bytes));
                    break;
                }
                Err(err) => panic!("pending reply failed: {err}"),
            }
        }
        if !progressed {
            std::thread::sleep(Duration::from_millis(1));
        }
    }
}
