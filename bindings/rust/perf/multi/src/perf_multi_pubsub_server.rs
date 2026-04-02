//! PUB/SUB multi server: PUB sender with POLLOUT backpressure.
//! Sends warmup → active → drain → stop.

mod common;

use std::time::{Duration, Instant};
use zlink::*;

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = Context::new().expect("context");
    let pub_sock = ctx.pub_socket().expect("pub");
    pub_sock.set_send_hwm(settings.hwm).expect("sndhwm");
    pub_sock.bind("tcp://0.0.0.0:0").expect("bind");
    let endpoint = pub_sock.last_endpoint().expect("endpoint");

    common::print_ready(&endpoint);

    // Wait for subscriptions to propagate (controlled by run script)
    std::thread::sleep(Duration::from_millis(500));

    let poller = Poller::new().expect("poller");
    poller.add_socket(&pub_sock, 0, POLLOUT).expect("poller add");

    let msg_size = args.msg_size;
    let mut buf = vec![0u8; msg_size.max(common::HEADER_SIZE)];
    let mut seq: u64 = 1;

    // Warmup
    send_phase(&pub_sock, &poller, &mut buf, &mut seq, msg_size,
               common::PHASE_WARMUP, Duration::from_secs(settings.warmup_seconds));

    // Active
    send_phase(&pub_sock, &poller, &mut buf, &mut seq, msg_size,
               common::PHASE_ACTIVE, Duration::from_secs(settings.duration_seconds));

    // Drain/stop token
    let stop_msg = Message::from_bytes(common::STOP_TOKEN).expect("stop");
    let _ = pub_sock.publish("P", stop_msg);

    common::print_server_queue_metrics("MULTI_PUBSUB", &args.transport);

    // Wait for stdin STOP from orchestrator
    common::wait_for_stop_stdin();
}

fn send_phase(pub_sock: &PubSocket, poller: &Poller, buf: &mut [u8], seq: &mut u64, msg_size: usize,
              phase: u32, duration: Duration) {
    let deadline = Instant::now() + duration;
    let mut pending = false;

    while Instant::now() < deadline {
        if !pending {
            common::encode_header(buf, phase, msg_size as u32, *seq);
            let msg = Message::from_bytes(buf).expect("msg");
            match pub_sock.try_publish("P", msg) {
                Ok(SendResult::Sent) => { *seq += 1; }
                _ => {
                    pending = true;
                    let _ = poller.modify_socket(pub_sock, POLLOUT);
                }
            }
        } else {
            let remain = (deadline - Instant::now()).as_millis() as i64;
            let wait = remain.min(100).max(1);
            let events = poller.wait_all(1, wait).unwrap_or_default();
            for ev in &events {
                if ev.is_writable() { pending = false; }
            }
        }
    }
}
