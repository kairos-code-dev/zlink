//! MULTI_SPOT server: one-way PUB sender through SPOT facade.
//! Sends active → stop token. POLLOUT for backpressure.

mod common;

use common::backpressure::SocketBackpressure;
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
    std::thread::sleep(Duration::from_millis(500));

    let poller = Poller::new().expect("poller");
    poller.add_socket(&pub_sock, 0, POLLOUT).expect("poller add");

    let msg_size = args.msg_size;
    let mut buf = vec![0u8; msg_size.max(common::HEADER_SIZE)];
    let mut seq: u64 = 1;

    send_phase(&pub_sock, &poller, &mut buf, &mut seq, msg_size,
               common::PHASE_ACTIVE, Duration::from_secs(settings.duration_seconds));

    let stop_deadline = Instant::now() + Duration::from_secs(1);
    while Instant::now() < stop_deadline {
        let stop = Message::from_bytes(common::STOP_TOKEN).expect("stop");
        let _ = pub_sock.publish("S", stop);
    }

}

fn send_phase(pub_sock: &PubSocket, poller: &Poller, buf: &mut [u8], seq: &mut u64, msg_size: usize,
              phase: u32, duration: Duration) {
    let deadline = Instant::now() + duration;
    let mut backpressure = SocketBackpressure::new();
    while Instant::now() < deadline {
        if !backpressure.is_pending() {
            common::encode_header(buf, phase, msg_size as u32, *seq);
            let msg = Message::from_bytes(buf).expect("msg");
            match pub_sock.try_publish("S", msg) {
                Ok(SendResult::Sent) => { *seq += 1; }
                _ => {
                    backpressure.mark_pending(|| {
                        let _ = poller.modify_socket(pub_sock, POLLOUT);
                    });
                }
            }
        } else {
            let remain = (deadline - Instant::now()).as_millis() as i64;
            let wait = remain.min(50).max(1);
            if let Ok(Some(ev)) = poller.wait(wait) {
                if ev.is_writable() {
                    backpressure.clear_pending(|| {
                        let _ = poller.modify_socket(pub_sock, 0);
                    });
                }
            }
        }
    }
}
