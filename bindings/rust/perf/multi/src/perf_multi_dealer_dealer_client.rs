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
        if matches!(args.transport.as_str(), "tls" | "wss") {
            let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
            common::setup_raw_tls_client(&sock, &tls).expect("client tls");
        }
        sock.connect(&args.endpoint).expect("connect");
        sockets.push(sock);
    }

    std::thread::sleep(Duration::from_millis(100));
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
    let mut active_count: u64 = 0;
    let mut latency_stats = common::LatencyStats::new();
    let mut index: usize = 0;

    while Instant::now() < deadline {
        let socket = &sockets[index % sockets.len()];
        common::encode_header(&mut buf, common::PHASE_ACTIVE, msg_size as u32, seq);
        let t0 = common::now_ns();
        let msg = Message::copy_from(&buf).expect("msg");
        match socket.send_with_flags(msg, SendFlags::DONT_WAIT) {
            Ok(()) => {
                seq += 1;
                active_count += 1;
                latency_stats.record_ns(common::now_ns().saturating_sub(t0) as f64);
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

    let stats = latency_stats.finish();
    let final_stats = common::StatsResult {
        count: active_count,
        ..stats
    };
    let key = format!(
        "RESULT,current,{},{},{}",
        "MULTI_DEALER_DEALER", &args.transport, msg_size
    );
    let phase_result =
        common::build_phase_result(msg_size, settings.duration_seconds, &final_stats);
    common::print_phase_result(&key, &phase_result);
}
