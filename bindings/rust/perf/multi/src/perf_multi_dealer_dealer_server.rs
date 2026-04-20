//! DEALER-DEALER multi server: one-way receive sink.

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
        .set_recv_timeout(Duration::from_millis(settings.recv_timeout_ms))
        .expect("rcvtimeo");
    if matches!(args.transport.as_str(), "tls" | "wss") {
        let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
        common::setup_raw_tls_server(&server, &tls).expect("server tls");
    }

    let bind_endpoint = common::resolve_server_bind_endpoint(&args.transport);
    if let Err(err) = server.bind(&bind_endpoint) {
        if common::handle_transport_setup_error(
            "MULTI_DEALER_DEALER",
            &args.transport,
            "bind",
            err,
        ) {
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

    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds);
    let mut latency = common::LatencyStats::new();
    let mut active_count = 0u64;
    while Instant::now() < deadline {
        loop {
            match server.recv_with_flags(RecvFlags::DONT_WAIT) {
                Ok(received) => {
                    let data = common::message_payload(received.parts());
                    if common::is_valid_active_message(data, args.msg_size) {
                        let sent_ts_ns = common::decode_sent_ts_ns(data);
                        latency.record_ns(
                            common::now_ns().saturating_sub(sent_ts_ns.max(0) as u64) as f64,
                        );
                        active_count += 1;
                    }
                }
                Err(err) if err.code == RecvResult::NoData => break,
                Err(_) => break,
            }
        }
    }

    let stats = latency.finish();
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
