#[path = "perf_common.rs"]
mod common;

use std::io::{self, BufRead};
use std::time::{Duration, Instant};
use zlink::*;

const TOPIC: &str = "bench";
const STOP_TOKEN_BURST: usize = 64;

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = common::perf_server_context();
    common::apply_multi_auto_hwm_msg_unit(&ctx, args.msg_size);
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
    let Some(bind_endpoint) = common::resolve_server_bind_endpoint("MULTI_PUBSUB", &args.transport)
    else {
        return;
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

    let poller = Poller::new().expect("poller");
    poller
        .add_socket(&pub_sock, POLLOUT, 0)
        .expect("poller add");
    let mut events = vec![PollEvent::default(); 1];
    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds);
    let payload_size = args.msg_size.max(common::HEADER_SIZE);
    let mut seq: u64 = 1;
    let mut pending = false;

    while Instant::now() < deadline {
        if !pending {
            let mut msg = Message::with_size(payload_size).expect("msg");
            common::encode_header(
                msg.data_mut(),
                common::PHASE_ACTIVE,
                args.msg_size as u32,
                seq,
            );
            match pub_sock
                .publish(TOPIC)
                .message(msg)
                .flags(SendFlags::DONT_WAIT)
                .submit()
            {
                Ok(true) => {
                    seq += 1;
                    continue;
                }
                Ok(false) => {
                    pending = true;
                }
                Err(err) if err.code() == SubmitResult::Backpressured => {
                    pending = true;
                }
                Err(err) => panic!("publish failed: {err}"),
            }
        }

        let remaining_ms = deadline
            .saturating_duration_since(Instant::now())
            .as_millis()
            .max(1) as i64;
        match poller.wait(&mut events, remaining_ms) {
            Ok(n) if n > 0 && events[0].is_writable() => {
                pending = false;
            }
            Ok(_) => {}
            Err(err) => panic!("poller wait failed: {err}"),
        }
    }

    // PERF_MULTI_TEST_POLICY § 1.3.1: signal phase end via wire-level stop
    // token. Match the C runner by retrying until the token is actually
    // accepted; otherwise subscribers can wait forever after large messages.
    let mut accepted_stop_tokens = 0usize;
    while accepted_stop_tokens < STOP_TOKEN_BURST {
        let token = Message::copy_from(common::STOP_TOKEN).expect("stop token");
        match pub_sock.publish(TOPIC).message(token).submit() {
            Ok(true) => {
                accepted_stop_tokens += 1;
            }
            Ok(false) => {
                std::thread::sleep(Duration::from_millis(1));
            }
            Err(err)
                if matches!(
                    err.code(),
                    SubmitResult::Backpressured | SubmitResult::NotConnected
                ) =>
            {
                std::thread::sleep(Duration::from_millis(1));
            }
            Err(err) => panic!("stop token publish failed: {err}"),
        }
    }
}
