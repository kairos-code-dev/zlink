#[path = "perf_common.rs"]
mod common;

use std::io::{self, BufRead, Write};
use std::time::{Duration, Instant};
use zlink::*;

const TOPIC: &str = "bench";

fn drain_subscriber(
    socket: &SubSocket,
    msg_size: usize,
    latency_stats: &mut common::LatencyStats,
    active_count: &mut u64,
) -> bool {
    let mut processed = false;
    loop {
        let mut topic_msg = TopicMessage::empty();
        match socket.subscribe(&mut topic_msg, RecvFlags::DONT_WAIT) {
            Ok(true) => {
                let data = common::message_payload(topic_msg.parts());
                if !common::is_valid_active_message(&data, msg_size) {
                    continue;
                }
                let sent_ts_ns = common::decode_sent_ts_ns(&data);
                latency_stats
                    .record_ns(common::now_ns().saturating_sub(sent_ts_ns.max(0) as u64) as f64);
                *active_count += 1;
                processed = true;
            }
            Ok(false) => break,
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
    let mut sockets: Vec<SubSocket> = Vec::with_capacity(settings.clients);

    for _ in 0..settings.clients {
        let sub = ctx.sub_socket().expect("sub");
        sub.common_options()
            .set_send_hwm(settings.send_hwm)
            .expect("sndhwm");
        sub.common_options()
            .set_recv_hwm(settings.recv_hwm)
            .expect("rcvhwm");
        if settings.msg_unit_bytes > 0 {
            sub.common_options()
                .set_auto_hwm_msg_unit_bytes(settings.msg_unit_bytes)
                .expect("auto hwm msg unit");
        }
        if matches!(args.transport.as_str(), "tls" | "wss") {
            let tls = common::resolve_perf_tls_paths().expect("TLS certs not found");
            common::setup_raw_tls_client(&sub, &tls).expect("client tls");
        }
        sub.connect(&args.endpoint).expect("connect");
        sub.set_subscription(TOPIC).expect("subscribe");
        sockets.push(sub);
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
    let mut latency_stats = common::LatencyStats::new();
    let mut active_count: u64 = 0;

    while Instant::now() < deadline {
        let mut progressed = false;
        for socket in &sockets {
            progressed |=
                drain_subscriber(socket, args.msg_size, &mut latency_stats, &mut active_count);
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
        "MULTI_PUBSUB",
        &args.transport,
        args.msg_size,
        settings.duration_seconds,
        &final_stats,
    );
    std::process::exit(0);
}
