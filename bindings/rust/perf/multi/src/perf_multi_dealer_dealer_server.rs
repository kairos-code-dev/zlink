//! DEALER-DEALER multi server: one-way receive sink.
//! Poller POLLIN drain loop, counts stop tokens from clients.

mod common;

use std::time::{Duration, Instant};
use zlink::*;

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = Context::new().expect("context");
    let server = ctx.dealer_socket().expect("dealer");
    server.common_options().set_recv_hwm(settings.hwm).expect("rcvhwm");
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

    let poller = Poller::new().expect("poller");
    poller.add_socket(&server, 0, POLLIN).expect("poller add");

    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds + 1);
    while Instant::now() < deadline {
        let remain_ms = (deadline - Instant::now()).as_millis() as i64;
        let wait_ms = remain_ms.min(100).max(1);

        match poller.wait(wait_ms) {
            Ok(Some(_ev)) => {
                // Drain all available messages (nonblocking)
                loop {
                    match server.try_recv() {
                        Ok(Some(received)) => {
                            let _ = received.parts()[0].data();
                        }
                        Ok(None) => break,
                        Err(_) => break,
                    }
                }
            }
            Ok(None) => continue, // timeout
            Err(_) => break,
        }
    }
}
