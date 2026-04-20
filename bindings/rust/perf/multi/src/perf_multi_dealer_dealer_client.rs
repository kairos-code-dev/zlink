//! DEALER-DEALER multi client: one-way send workload with N client sockets.

mod common;

use std::io::{self, BufRead, Write};
use std::time::{Duration, Instant};
use zlink::*;

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = Context::new().expect("context");
    let mut sockets: Vec<DealerSocket> = Vec::with_capacity(settings.clients);

    for _ in 0..settings.clients {
        let sock = ctx.dealer_socket().expect("dealer");
        sock.common_options().set_send_hwm(settings.hwm).expect("sndhwm");
        sock.common_options().set_recv_hwm(settings.hwm).expect("rcvhwm");
        if matches!(args.transport.as_str(), "tls" | "wss") {
            let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
            common::setup_raw_tls_client(&sock, &tls).expect("client tls");
        }
        sock.connect(&args.endpoint).expect("connect");
        sockets.push(sock);
    }

    let mut monitors: Vec<SocketMonitor> = Vec::with_capacity(settings.clients);
    for socket in &sockets {
        monitors.push(SocketMonitor::open(socket).expect("monitor"));
    }
    for monitor in &monitors {
        loop {
            match monitor.recv() {
                Ok(event) if event.is_connection_ready() => break,
                Ok(_) => continue,
                Err(err) => panic!("connection-ready wait failed: {err}"),
            }
        }
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
    }
    if !start_seen {
        return;
    }

    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds);
    let msg_size = args.msg_size;
    let mut buf = vec![0u8; msg_size.max(common::HEADER_SIZE)];
    let mut seq: u64 = 1;
    let mut index: usize = 0;

    while Instant::now() < deadline {
        let socket = &sockets[index % sockets.len()];
        common::encode_header(&mut buf, common::PHASE_ACTIVE, msg_size as u32, seq);
        let msg = Message::copy_from(&buf).expect("msg");
        match socket.send_with_flags(msg, SendFlags::DONT_WAIT) {
            Ok(()) => {
                seq += 1;
            }
            Err(err) if err.code == SubmitResult::Backpressured => {}
            Err(err) if err.code == SubmitResult::NotConnected => {}
            Err(err) => panic!("send failed: {err}"),
        }
        index += 1;
        if (index & 0x3ff) == 0 {
            std::thread::yield_now();
        }
    }
}
