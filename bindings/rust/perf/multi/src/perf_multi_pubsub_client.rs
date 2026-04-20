#[path = "perf_common.rs"]
mod common;

use std::io::{self, BufRead, Write};
use std::time::{Duration, Instant};
use zlink::*;

const TOPIC: &str = "perf.topic";

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = common::perf_client_context();
    let mut sockets: Vec<SubSocket> = Vec::with_capacity(settings.clients);
    let poller = Poller::new().expect("poller");
    let mut socket_keys = Vec::with_capacity(settings.clients);

    for _ in 0..settings.clients {
        let sub = ctx.sub_socket().expect("sub");
        sub.common_options()
            .set_send_hwm(settings.send_hwm)
            .expect("sndhwm");
        sub.common_options()
            .set_recv_hwm(settings.recv_hwm)
            .expect("rcvhwm");
        if matches!(args.transport.as_str(), "tls" | "wss") {
            let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
            common::setup_raw_tls_client(&sub, &tls).expect("client tls");
        }
        sub.connect(&args.endpoint).expect("connect");
        sub.set_subscription(TOPIC).expect("subscribe");
        poller.add_socket(&sub, POLLIN).expect("poller add");
        socket_keys.push(common::raw_socket_handle_sub(&sub) as usize);
        sockets.push(sub);
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
    let mut latency_stats = common::LatencyStats::new();
    let mut active_count: u64 = 0;

    while Instant::now() < deadline {
        let events =
            common::wait_native_poll_events(&poller, sockets.len(), 25).expect("poller wait all");
        for event in events {
            let Some(index) = socket_keys
                .iter()
                .position(|socket_key| *socket_key == event.socket as usize)
            else {
                continue;
            };
            if event.events & POLLIN == 0 {
                continue;
            }
            loop {
                match sockets[index].subscribe_with_flags(RecvFlags::DONT_WAIT) {
                    Ok(topic_msg) => {
                        let data = common::message_payload(topic_msg.parts());
                        if !common::is_valid_active_message(data, args.msg_size) {
                            continue;
                        }
                        let sent_ts_ns = common::decode_sent_ts_ns(data);
                        latency_stats.record_ns(
                            common::now_ns().saturating_sub(sent_ts_ns.max(0) as u64) as f64,
                        );
                        active_count += 1;
                    }
                    Err(err) if err.code == RecvResult::NoData => break,
                    Err(err) => panic!("recv failed: {err}"),
                }
            }
        }
    }

    let stats = latency_stats.finish();
    let final_stats = common::StatsResult {
        count: active_count,
        ..stats
    };
    common::print_result(
        "MULTI_PUBSUB",
        &args.transport,
        args.msg_size,
        settings.duration_seconds,
        &final_stats,
    );
}
