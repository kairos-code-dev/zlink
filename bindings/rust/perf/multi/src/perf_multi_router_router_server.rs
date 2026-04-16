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
    let ctx = Context::new().expect("context");
    let router = ctx.router_socket().expect("router");
    let rid = RoutingId::from_bytes(b"perf-rr-server");
    router.set_routing_id(&rid).expect("set rid");
    router.common_options().set_send_hwm(settings.hwm).expect("sndhwm");
    router.common_options().set_recv_hwm(settings.hwm).expect("rcvhwm");
    router
        .common_options()
        .set_recv_timeout(Duration::from_millis(1))
        .expect("recv timeout");
    if matches!(args.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        common::setup_raw_tls_server(&router, &tls).expect("server tls");
    }
    let bind_endpoint = match args.transport.as_str() {
        "ws" => "ws://0.0.0.0:0".to_string(),
        "wss" => "wss://0.0.0.0:0".to_string(),
        "tls" => "tls://0.0.0.0:0".to_string(),
        _ => "tcp://0.0.0.0:0".to_string(),
    };
    router.bind(&bind_endpoint).expect("bind");
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
