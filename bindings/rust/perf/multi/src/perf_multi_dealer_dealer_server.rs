//! DEALER-DEALER multi server: one-way active sender.

#[path = "perf_common.rs"]
mod common;

use std::io::{self, BufRead};
use std::time::{Duration, Instant};
use zlink::*;

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = common::perf_server_context();
    let server = ctx.dealer_socket().expect("dealer");
    server
        .common_options()
        .set_send_hwm(settings.send_hwm)
        .expect("sndhwm");
    server
        .common_options()
        .set_recv_hwm(settings.recv_hwm)
        .expect("rcvhwm");
    server
        .common_options()
        .set_send_timeout(Duration::from_millis(settings.send_timeout_ms))
        .expect("sndtimeo");
    if matches!(args.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        common::setup_raw_tls_server(&server, &tls).expect("server tls");
    }

    let Some(bind_endpoint) =
        common::resolve_server_bind_endpoint("MULTI_DEALER_DEALER", &args.transport)
    else {
        return;
    };
    if let Err(err) = server.bind(&bind_endpoint) {
        if common::handle_transport_setup_error("MULTI_DEALER_DEALER", &args.transport, "bind", err)
        {
            return;
        }
        panic!("bind: {err}");
    }
    let endpoint = server.last_endpoint().expect("endpoint");
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
    poller.add_socket(&server, POLLOUT).expect("poller add");
    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds);
    let mut buf = vec![0u8; args.msg_size.max(common::HEADER_SIZE)];
    let mut seq: u64 = 1;
    let mut pending = false;

    while Instant::now() < deadline {
        if !pending {
            common::encode_header(&mut buf, common::PHASE_ACTIVE, args.msg_size as u32, seq);
            let msg = Message::copy_from(&buf).expect("msg");
            match server.send_with_flags(msg, SendFlags::DONT_WAIT) {
                Ok(()) => {
                    seq += 1;
                    continue;
                }
                Err(err) if err.code == SubmitResult::Backpressured => {
                    pending = true;
                }
                Err(err) if err.code == SubmitResult::NotConnected => {}
                Err(err) => panic!("send failed: {err}"),
            }
        }

        match poller.wait(25) {
            Ok(Some(event)) if event.is_writable() => {
                pending = false;
            }
            Ok(Some(_)) | Ok(None) => {}
            Err(err) => panic!("poller wait failed: {err}"),
        }
    }
}
