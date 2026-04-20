//! DEALER-DEALER multi client: one-way send workload with N client sockets.

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
    let poller = Poller::new().expect("poller");
    let mut send_pending = vec![false; settings.clients];
    let mut socket_keys = Vec::with_capacity(settings.clients);

    for _ in 0..settings.clients {
        let sock = ctx.dealer_socket().expect("dealer");
        sock.common_options()
            .set_send_hwm(settings.send_hwm)
            .expect("sndhwm");
        sock.common_options()
            .set_recv_hwm(settings.recv_hwm)
            .expect("rcvhwm");
        if matches!(args.transport.as_str(), "tls" | "wss") {
            let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
            common::setup_raw_tls_client(&sock, &tls).expect("client tls");
        }
        sock.connect(&args.endpoint).expect("connect");
        poller.add_socket(&sock, POLLOUT).expect("poller add");
        socket_keys.push(common::raw_socket_handle_dealer(&sock) as usize);
        sockets.push(sock);
    }

    let mut monitors: Vec<SocketMonitor> = Vec::with_capacity(settings.clients);
    for socket in &sockets {
        monitors.push(SocketMonitor::open(socket).expect("monitor"));
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
        if matches!(
            line.trim(),
            text if text == format!("START,{}", args.msg_size)
                || text == format!("PHASE_ACTIVE,{}", args.msg_size)
        ) {
            start_seen = true;
            break;
        }
    }
    if !start_seen {
        return;
    }

    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds);
    let msg_size = args.msg_size;
    let mut buf = vec![0u8; msg_size.max(common::HEADER_SIZE)];
    let mut seq: u64 = 1;

    while Instant::now() < deadline {
        let mut progressed = false;
        for (index, socket) in sockets.iter().enumerate() {
            if send_pending[index] {
                continue;
            }
            common::encode_header(&mut buf, common::PHASE_ACTIVE, msg_size as u32, seq);
            let msg = Message::copy_from(&buf).expect("msg");
            match socket.send_with_flags(msg, SendFlags::DONT_WAIT) {
                Ok(()) => {
                    seq += 1;
                    progressed = true;
                }
                Err(err) if err.code == SubmitResult::Backpressured => {
                    send_pending[index] = true;
                }
                Err(err) if err.code == SubmitResult::NotConnected => {}
                Err(err) => panic!("send failed: {err}"),
            }
        }
        if progressed {
            continue;
        }

        let events = common::wait_native_poll_events(&poller, sockets.len(), 25)
            .expect("poller wait all");
        for event in events {
            if event.events & POLLOUT == 0 {
                continue;
            }
            if let Some(index) = socket_keys
                .iter()
                .position(|socket_key| *socket_key == event.socket as usize)
            {
                send_pending[index] = false;
            }
        }
    }
}
