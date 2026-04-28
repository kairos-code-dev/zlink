//! DEALER-DEALER multi client: one-way active receiver with N dealer sockets.

#[path = "perf_common.rs"]
mod common;

use std::io::{self, BufRead, Write};
use std::time::{Duration, Instant};
use zlink::*;

fn drain_socket(
    socket: &DealerSocket,
    msg_size: usize,
    latency_stats: &mut common::LatencyStats,
    active_count: &mut u64,
) -> bool {
    let mut processed = false;
    loop {
        match socket.recv_with_flags(RecvFlags::DONT_WAIT) {
            Ok(received) => {
                let data = common::message_payload(received.parts());
                if !common::is_valid_active_message(data, msg_size) {
                    continue;
                }
                let sent_ts_ns = common::decode_sent_ts_ns(data);
                latency_stats
                    .record_ns(common::now_ns().saturating_sub(sent_ts_ns.max(0) as u64) as f64);
                *active_count += 1;
                processed = true;
            }
            Err(err) if err.code == RecvResult::NoData => break,
            Err(err) => panic!("recv failed: {err}"),
        }
    }
    processed
}

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = common::perf_client_context();
    let mut sockets: Vec<DealerSocket> = Vec::with_capacity(settings.clients);
    let mut monitors: Vec<SocketMonitor> = Vec::with_capacity(settings.clients);

    for _ in 0..settings.clients {
        let sock = ctx.dealer_socket().expect("dealer");
        sock.common_options()
            .set_send_hwm(settings.send_hwm)
            .expect("sndhwm");
        sock.common_options()
            .set_recv_hwm(settings.recv_hwm)
            .expect("rcvhwm");
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

    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds);
    let mut latency_stats = common::LatencyStats::new();
    let mut active_count: u64 = 0;

    while Instant::now() < deadline {
        let mut progressed = false;
        for socket in &sockets {
            progressed |=
                drain_socket(socket, args.msg_size, &mut latency_stats, &mut active_count);
        }
        if !progressed {
            std::thread::sleep(Duration::from_millis(1));
        }
    }

    let stats = latency_stats.finish();
    let final_stats = common::StatsResult {
        count: active_count,
        ..stats
    };
    common::print_result(
        "MULTI_DEALER_DEALER",
        &args.transport,
        args.msg_size,
        settings.duration_seconds,
        &final_stats,
    );
}
