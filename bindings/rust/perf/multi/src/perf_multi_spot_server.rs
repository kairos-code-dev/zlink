mod common;

use std::time::{Duration, Instant};
use zlink::*;

const TOPIC: &str = "perf.spot.topic";

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = Context::new().expect("context");
    let pub_sock = ctx.pub_socket().expect("pub");
    pub_sock.common_options().set_send_hwm(settings.hwm).expect("sndhwm");
    let bind_endpoint = match args.transport.as_str() {
        "ws" => "ws://0.0.0.0:0".to_string(),
        "wss" => "wss://0.0.0.0:0".to_string(),
        "tls" => "tls://0.0.0.0:0".to_string(),
        _ => "tcp://0.0.0.0:0".to_string(),
    };
    pub_sock.bind(&bind_endpoint).expect("bind");
    let endpoint = pub_sock.last_endpoint().expect("endpoint");
    common::print_ready(&endpoint);

    std::thread::sleep(Duration::from_millis(500));

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
