#[path = "perf_common.rs"]
mod common;

use std::io::{self, BufRead};
use std::time::{Duration, Instant};
use zlink::*;

const TOPIC: &str = "perf.topic";

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = common::perf_server_context();
    let pub_sock = ctx.pub_socket().expect("pub");
    pub_sock
        .common_options()
        .set_send_hwm(settings.send_hwm)
        .expect("sndhwm");
    pub_sock
        .common_options()
        .set_recv_hwm(settings.recv_hwm)
        .expect("rcvhwm");
    pub_sock
        .common_options()
        .set_send_timeout(Duration::from_millis(settings.send_timeout_ms))
        .expect("sndtimeo");
    if matches!(args.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        common::setup_raw_tls_server(&pub_sock, &tls).expect("server tls");
    }
    let bind_endpoint = common::resolve_server_bind_endpoint(&args.transport);
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
            Err(err) if err.code == SubmitResult::Backpressured => {}
            Err(err) => panic!("publish failed: {err}"),
        }
    }
}
