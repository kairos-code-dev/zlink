mod common;

use std::io::{self, BufRead};
use std::time::{Duration, Instant};
use zlink::*;

const TOPIC: &str = "perf.topic";

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = Context::new().expect("context");
    let pub_sock = ctx.pub_socket().expect("pub");
    pub_sock.common_options().set_send_hwm(settings.hwm).expect("sndhwm");
    pub_sock.common_options().set_recv_hwm(settings.hwm).expect("rcvhwm");
    let bind_endpoint = match args.transport.as_str() {
        "ws" => "ws://0.0.0.0:0".to_string(),
        "wss" => "wss://0.0.0.0:0".to_string(),
        "tls" => "tls://0.0.0.0:0".to_string(),
        _ => "tcp://0.0.0.0:0".to_string(),
    };
    if let Err(err) = pub_sock.bind(&bind_endpoint) {
        if common::handle_transport_setup_error("MULTI_PUBSUB", &args.transport, "bind", err) {
            return;
        }
        panic!("bind: {err}");
    }
    let endpoint = pub_sock.last_endpoint().expect("endpoint");
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

    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds);
    let mut buf = vec![0u8; args.msg_size.max(common::HEADER_SIZE)];
    let mut seq: u64 = 1;

    while Instant::now() < deadline {
        common::encode_header(&mut buf, common::PHASE_ACTIVE, args.msg_size as u32, seq);
        seq += 1;
        let msg = Message::copy_from(&buf).expect("msg");
        match pub_sock.publish_with_flags(TOPIC, msg, SendFlags::DONT_WAIT) {
            Ok(()) => {}
            Err(err) if err.code == SubmitResult::Backpressured => {
                std::thread::yield_now();
            }
            Err(err) => panic!("publish failed: {err}"),
        }
    }
}
