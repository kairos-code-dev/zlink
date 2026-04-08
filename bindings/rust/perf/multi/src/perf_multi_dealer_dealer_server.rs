//! DEALER-DEALER multi server: one-way receive sink.
//! Poller POLLIN drain loop, counts stop tokens from clients.

mod common;

use std::time::{Duration, Instant};
use zlink::*;

fn main() {
    let _args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = Context::new().expect("context");
    let server = ctx.dealer_socket().expect("dealer");
    server.common_options().set_recv_hwm(settings.hwm).expect("rcvhwm");
    server.bind("tcp://0.0.0.0:0").expect("bind");
    let endpoint = server.last_endpoint().expect("endpoint");

    common::print_ready(&endpoint);

    let poller = Poller::new().expect("poller");
    poller.add_socket(&server, 0, POLLIN).expect("poller add");

    let deadline = Instant::now()
        + Duration::from_secs(settings.duration_seconds + 30);

    let mut stops = 0usize;
    let target_stops = settings.clients;

    while stops < target_stops && Instant::now() < deadline {
        let remain_ms = (deadline - Instant::now()).as_millis() as i64;
        let wait_ms = remain_ms.min(100).max(1);

        match poller.wait(wait_ms) {
            Ok(Some(_ev)) => {
                // Drain all available messages (nonblocking)
                loop {
                    match server.try_recv() {
                        Ok(Some(received)) => {
                            let data = received.parts()[0].data();
                            if common::is_stop_token(data) {
                                stops += 1;
                                if stops >= target_stops { break; }
                            }
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
    // Wait for stdin STOP from orchestrator (or just exit on deadline)
}
