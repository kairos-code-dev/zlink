//! DEALER-ROUTER multi server: echo server.
//! Poller POLLIN drain → echo back via routed send. POLLOUT for backpressure.

mod common;

use common::backpressure::SocketBackpressure;
use std::collections::VecDeque;
use std::time::{Duration, Instant};
use zlink::*;

fn main() {
    let _args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = Context::new().expect("context");
    let router = ctx.router_socket().expect("router");
    router.common_options().set_recv_hwm(settings.hwm).expect("rcvhwm");
    router.common_options().set_send_hwm(settings.hwm).expect("sndhwm");
    router.bind("tcp://0.0.0.0:0").expect("bind");
    let endpoint = router.last_endpoint().expect("endpoint");

    common::print_ready(&endpoint);

    let poller = Poller::new().expect("poller");
    poller.add_socket(&router, 0, POLLIN).expect("poller add");

    let deadline = Instant::now()
        + Duration::from_secs(settings.duration_seconds + 30);

    let mut stops = 0usize;
    let target_stops = settings.clients;
    let mut pending: VecDeque<(RoutingId, Vec<u8>)> = VecDeque::new();
    let mut backpressure = SocketBackpressure::new();

    while stops < target_stops && Instant::now() < deadline {
        let remain_ms = (deadline - Instant::now()).as_millis() as i64;
        let wait_ms = remain_ms.min(100).max(1);
        let events = poller.wait_all(1, wait_ms).unwrap_or_default();

        for ev in &events {
            if ev.is_readable() {
                // Drain recv
                loop {
                    match router.try_recv() {
                        Ok(Some(received)) => {
                            let data = received.parts()[0].data();
                            if common::is_stop_token(data) {
                                stops += 1;
                                if stops >= target_stops { break; }
                                continue;
                            }
                            let rid = received.routing_id().clone();
                            let payload = data.to_vec();
                            // Try echo immediately
                            let msg = Message::from_bytes(&payload).expect("msg");
                            match router.try_send(&rid, msg) {
                                Ok(SendResult::Sent) => {}
                                _ => {
                                    pending.push_back((rid, payload));
                                    backpressure.mark_pending(|| {
                                        let _ = poller.modify_socket(&router, POLLIN | POLLOUT);
                                    });
                                }
                            }
                        }
                        Ok(None) => break,
                        Err(_) => break,
                    }
                }
            }
            if ev.is_writable() && !pending.is_empty() {
                // Drain pending
                while let Some((rid, payload)) = pending.front() {
                    let msg = Message::from_bytes(payload).expect("msg");
                    match router.try_send(rid, msg) {
                        Ok(SendResult::Sent) => { pending.pop_front(); }
                        _ => break, // still backpressured
                    }
                }
                if pending.is_empty() && backpressure.is_pending() {
                    backpressure.clear_pending(|| {
                        let _ = poller.modify_socket(&router, POLLIN);
                    });
                }
            }
        }
    }
}
