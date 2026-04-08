//! MULTI_STREAM server: poll-based echo relay.
//! STREAM socket bind, echo incoming payloads back to originating peer.

mod common;

use common::backpressure::SocketBackpressure;
use std::collections::VecDeque;
use std::time::{Duration, Instant};
use zlink::*;

fn main() {
    let _args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = Context::new().expect("context");
    let stream = ctx.stream_socket().expect("stream");
    stream.set_recv_hwm(settings.hwm).expect("rcvhwm");
    stream.set_send_hwm(settings.hwm).expect("sndhwm");
    stream.set_notify(true).expect("notify");
    stream.bind("tcp://0.0.0.0:0").expect("bind");
    let endpoint = stream.last_endpoint().expect("endpoint");

    common::print_ready(&endpoint);

    let poller = Poller::new().expect("poller");
    poller.add_socket(&stream, 0, POLLIN).expect("poller add");

    let deadline = Instant::now()
        + Duration::from_secs(settings.duration_seconds + 30);

    let mut stop_seen = false;
    let mut pending: VecDeque<(RoutingId, Vec<u8>)> = VecDeque::new();
    let mut backpressure = SocketBackpressure::new();

    while !stop_seen && Instant::now() < deadline {
        let remain = (deadline - Instant::now()).as_millis() as i64;
        let wait = remain.min(100).max(1);
        match poller.wait(wait) {
            Ok(Some(ev)) => {
                if ev.is_readable() {
                    loop {
                        match stream.try_recv() {
                            Ok(Some(received)) => {
                                let data = received.parts()[0].data();
                                if data.is_empty() { continue; } // connect/disconnect notification
                                if common::is_stop_token(data) { stop_seen = true; break; }
                                let rid = received.routing_id().clone();
                                let payload = data.to_vec();
                                let msg = Message::from_bytes(&payload).expect("msg");
                                match stream.try_send(&rid, msg) {
                                    Ok(SendResult::Sent) => {}
                                    _ => {
                                        pending.push_back((rid, payload));
                                        backpressure.mark_pending(|| {
                                            let _ = poller.modify_socket(&stream, POLLIN | POLLOUT);
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
                    while let Some((rid, payload)) = pending.front() {
                        let msg = Message::from_bytes(payload).expect("msg");
                        match stream.try_send(rid, msg) {
                            Ok(SendResult::Sent) => { pending.pop_front(); }
                            _ => break,
                        }
                    }
                    if pending.is_empty() && backpressure.is_pending() {
                        backpressure.clear_pending(|| {
                            let _ = poller.modify_socket(&stream, POLLIN);
                        });
                    }
                }
            }
            Ok(None) => continue,
            Err(_) => break,
        }
    }
}
