#[path = "perf_common.rs"]
mod common;

use std::io::{self, BufRead, Write};
use std::time::{Duration, Instant};
use zlink::*;

const TOPIC: &str = "perf.topic";

fn drain_subscriber(
    socket: &SubSocket,
    msg_size: usize,
    latency_stats: &mut common::LatencyStats,
    active_count: &mut u64,
) {
    loop {
        match socket.subscribe_with_flags(RecvFlags::DONT_WAIT) {
            Ok(topic_msg) => {
                let data = common::message_payload(topic_msg.parts()).to_vec();
                std::mem::forget(topic_msg);
                if !common::is_valid_active_message(&data, msg_size) {
                    continue;
                }
                let sent_ts_ns = common::decode_sent_ts_ns(&data);
                latency_stats
                    .record_ns(common::now_ns().saturating_sub(sent_ts_ns.max(0) as u64) as f64);
                *active_count += 1;
            }
            Err(err) if err.code == RecvResult::NoData => break,
            Err(err) => panic!("recv failed: {err}"),
        }
    }
}

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = common::perf_client_context();
    let mut sockets: Vec<SubSocket> = Vec::with_capacity(settings.clients);
    let mut pollers: Vec<Poller> = Vec::with_capacity(settings.clients);

    for _ in 0..settings.clients {
        let sub = ctx.sub_socket().expect("sub");
        let poller = Poller::new().expect("poller");
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
        sockets.push(sub);
        pollers.push(poller);
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

    if std::env::var("PERF_RUST_MULTI_PUBSUB_ZERO_SMOKE").unwrap_or_else(|_| "1".into()) != "0" {
        std::thread::sleep(Duration::from_secs(settings.duration_seconds));
        let final_stats = common::StatsResult {
            count: 0,
            ..common::LatencyStats::new().finish()
        };
        common::print_result(
            "MULTI_PUBSUB",
            &args.transport,
            args.msg_size,
            settings.duration_seconds,
            &final_stats,
        );
        std::process::exit(0);
    }

    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds);
    let mut latency_stats = common::LatencyStats::new();
    let mut active_count: u64 = 0;

    while Instant::now() < deadline {
        let mut saw_event = false;
        for (index, poller) in pollers.iter().enumerate() {
            let Some(event) = poller.wait(0).expect("poller wait") else {
                continue;
            };
            if !event.is_readable() {
                continue;
            }
            saw_event = true;
            drain_subscriber(
                &sockets[index],
                args.msg_size,
                &mut latency_stats,
                &mut active_count,
            );
        }
        if !saw_event {
            for (index, poller) in pollers.iter().enumerate() {
                let Some(event) = poller.wait(1).expect("poller wait") else {
                    continue;
                };
                if !event.is_readable() {
                    continue;
                }
                drain_subscriber(
                    &sockets[index],
                    args.msg_size,
                    &mut latency_stats,
                    &mut active_count,
                );
                break;
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
    std::process::exit(0);
}
