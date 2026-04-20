#[path = "perf_common.rs"]
mod common;

use std::io::{self, BufRead};
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc,
};
use std::time::Duration;
use zlink::*;

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();
    let ctx = common::perf_server_context();
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
        .set_recv_timeout(Duration::from_millis(settings.recv_timeout_ms))
        .expect("recv timeout");
    if matches!(args.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        common::setup_raw_tls_server(&router, &tls).expect("server tls");
    }
    let Some(bind_endpoint) =
        common::resolve_server_bind_endpoint("MULTI_DEALER_ROUTER", &args.transport)
    else {
        return;
    };
    if let Err(err) = router.bind(&bind_endpoint) {
        if common::handle_transport_setup_error(
            "MULTI_DEALER_ROUTER",
            &args.transport,
            "bind",
            err,
        ) {
            return;
        }
        panic!("bind: {err}");
    }
    let endpoint = router.last_endpoint().expect("endpoint");
    common::print_ready(&endpoint);
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
    while !stop.load(Ordering::Acquire) {
        match router.recv() {
            Ok(received) => {
                let rid = received.routing_id().cloned();
                let reply = Message::copy_from(common::message_payload(received.parts())).expect("reply");
                if let Some(rid) = rid {
                    let _ = router.send(&rid, reply);
                }
            }
            Err(_) => {}
        }
    }
}
