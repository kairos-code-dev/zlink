//! DEALER-DEALER multi client: one-way active sender with N dealer sockets.

#[path = "perf_common.rs"]
mod common;

use std::io::{self, BufRead, Write};
use std::time::{Duration, Instant};
use zlink::*;

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = common::perf_client_context();
    let mut sockets: Vec<DealerSocket> = Vec::with_capacity(settings.clients);
    let mut monitors: Vec<SocketMonitor> = Vec::with_capacity(settings.clients);

    for _ in 0..settings.clients {
        let sock = ctx.dealer_socket().expect("dealer");
        // C: gated numeric HWM + unconditional AUTO_HWM_MSG_UNIT_BYTES.
        common::apply_multi_hwm(&sock, &settings);
        common::apply_multi_auto_hwm_msg_unit(&sock, args.msg_size);
        sock.common_options()
            .set_recv_timeout(Duration::from_millis(settings.recv_timeout_ms))
            .expect("rcvtimeo");
        if matches!(args.transport.as_str(), "tls" | "wss") {
            let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
            common::setup_raw_tls_client(&sock, &tls).expect("client tls");
        }
        let mon = SocketMonitor::open(&sock).expect("monitor");
        sock.connect(&args.endpoint).expect("connect");
        sockets.push(sock);
        monitors.push(mon);
    }

    let ready_timeout = common::resolve_multi_connect_ready_timeout();
    for monitor in &mut monitors {
        common::wait_monitor_ready(monitor, ready_timeout, "multi dealer-dealer client");
    }

    println!("CLIENT_READY,{}", args.msg_size);
    io::stdout().flush().ok();

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
    for socket in &sockets {
        poller.add_socket(socket, POLLOUT).expect("poller add");
    }

    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds);
    let mut payloads = (0..sockets.len())
        .map(|_| vec![0u8; args.msg_size.max(common::HEADER_SIZE)])
        .collect::<Vec<_>>();
    let mut seq: u64 = 1;
    while Instant::now() < deadline {
        let mut progressed = false;
        for (index, socket) in sockets.iter().enumerate() {
            if Instant::now() >= deadline {
                break;
            }
            common::encode_header(
                &mut payloads[index],
                common::PHASE_ACTIVE,
                args.msg_size as u32,
                seq,
            );
            let msg = Message::copy_from(&payloads[index]).expect("msg");
            match socket
                .send()
                .message(msg)
                .flags(SendFlags::DONT_WAIT)
                .submit()
            {
                Ok(true) => {
                    seq += 1;
                    progressed = true;
                }
                Ok(false) => {}
                Err(err) if err.code() == SubmitResult::Backpressured => {}
                Err(err) if err.code() == SubmitResult::NotConnected => {}
                Err(err) => panic!("send failed: {err}"),
            }
        }
        if !progressed {
            let remaining_ms = deadline
                .saturating_duration_since(Instant::now())
                .as_millis()
                .max(1) as i64;
            match poller.wait(remaining_ms) {
                Ok(Some(_)) | Ok(None) => {}
                Err(err) => panic!("poller wait failed: {err}"),
            }
        }
    }

    // C perf_multi_dealer_dealer_client.cpp run_single_size_case(): after the
    // send window, publish the wire stop token on every socket so the server's
    // poll wakes (the stop token is NOT the server's count anchor — its
    // measure-seconds deadline is). Bounded retry through transient backpressure.
    for socket in &sockets {
        let mut sent = false;
        for _ in 0..100 {
            match socket
                .send()
                .message(Message::copy_from(common::STOP_TOKEN).expect("stop token"))
                .submit()
            {
                Ok(_) => {
                    sent = true;
                    break;
                }
                Err(err)
                    if matches!(
                        err.code(),
                        SubmitResult::Backpressured | SubmitResult::NotConnected
                    ) =>
                {
                    std::thread::sleep(Duration::from_millis(1));
                }
                Err(err) => panic!("stop token send failed: {err}"),
            }
        }
        let _ = sent;
    }

    println!("CLIENT_DONE,{}", args.msg_size);
    io::stdout().flush().ok();
}
