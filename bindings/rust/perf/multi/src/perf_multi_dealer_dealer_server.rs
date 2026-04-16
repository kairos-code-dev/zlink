//! DEALER-DEALER multi server: one-way receive sink.

mod common;

use std::io::{self, BufRead};
use std::time::{Duration, Instant};
use zlink::*;

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = Context::new().expect("context");
    let server = ctx.dealer_socket().expect("dealer");
    server.common_options().set_recv_hwm(settings.hwm).expect("rcvhwm");
    server
        .common_options()
        .set_recv_timeout(Duration::from_millis(1))
        .expect("recv timeout");
    if matches!(args.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        common::setup_raw_tls_server(&server, &tls).expect("server tls");
    }

    let bind_endpoint = match args.transport.as_str() {
        "ws" => "ws://0.0.0.0:0".to_string(),
        "wss" => "wss://0.0.0.0:0".to_string(),
        "tls" => "tls://0.0.0.0:0".to_string(),
        _ => "tcp://0.0.0.0:0".to_string(),
    };
    server.bind(&bind_endpoint).expect("bind");
    let endpoint = server.last_endpoint().expect("endpoint");
    common::print_ready(&endpoint);

    let stdin = io::stdin();
    let mut start_seen = false;
    for line in stdin.lock().lines() {
        let line = line.unwrap_or_default();
        if line.trim() == format!("START,{}", args.msg_size) {
            start_seen = true;
            break;
        }
        if matches!(line.trim(), "STOP" | "QUIT") {
            return;
        }
    }
    if !start_seen {
        return;
    }

    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds + 1);
    while Instant::now() < deadline {
        loop {
            match server.recv_with_flags(RecvFlags::DONT_WAIT) {
                Ok(received) => {
                    let _ = received.parts();
                }
                Err(err) if err.code == RecvResult::NoData => break,
                Err(_) => break,
            }
        }
        std::thread::sleep(Duration::from_millis(1));
    }
}
