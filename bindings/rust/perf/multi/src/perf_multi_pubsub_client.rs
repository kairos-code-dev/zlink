#[path = "perf_common.rs"]
mod common;

use std::io::{self, BufRead, Write};
use std::time::{Duration, Instant};
use zlink::*;

const TOPIC: &str = "bench";

// Returns (processed_any, stop_seen). C perf_multi_pubsub_client.cpp
// recv_one_pubsub_message(): the wire stop token (pubsub_recv_stop) ends the
// recv phase.
fn drain_subscriber(
    socket: &SubSocket,
    msg_size: usize,
    latency_stats: &mut common::LatencyStats,
    active_count: &mut u64,
    active_deadline: Instant,
) -> bool {
    let mut stop_seen = false;
    loop {
        let mut topic_msg = TopicMessage::empty();
        match socket.subscribe(&mut topic_msg, RecvFlags::DONT_WAIT) {
            Ok(true) => {
                let data = common::message_payload(topic_msg.parts());
                if common::is_stop_token(data) {
                    stop_seen = true;
                    continue;
                }
                if !common::is_valid_active_message(data, msg_size) {
                    continue;
                }
                // C: messages arriving after the active deadline are not counted.
                if Instant::now() >= active_deadline {
                    continue;
                }
                let sent_ts_ns = common::decode_sent_ts_ns(data);
                latency_stats
                    .record_ns(common::now_ns().saturating_sub(sent_ts_ns.max(0) as u64) as f64);
                *active_count += 1;
            }
            Ok(false) => break,
            Err(err) if err.code == RecvResult::NoData => break,
            Err(err) => panic!("recv failed: {err}"),
        }
    }
    stop_seen
}

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = common::perf_client_context();
    common::apply_multi_auto_hwm_msg_unit(&ctx, args.msg_size);
    let mut sockets: Vec<SubSocket> = Vec::with_capacity(settings.clients);

    for _ in 0..settings.clients {
        let sub = ctx.sub_socket().expect("sub");
        // C parity: numeric HWM remains behind the manual-override gate.
        common::apply_multi_hwm(&sub, &settings);
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

    // C perf_multi_pubsub_client.cpp run_recv_duration(): zlink_poller_wait(-1)
    // signal-driven; the phase ends on the wire stop token (not a wall clock).
    // No hot-loop sleep(1ms).
    let poller = Poller::new().expect("poller");
    for socket in &sockets {
        poller.add_socket(socket, POLLIN, 0).expect("poller add");
    }
    let mut events = vec![PollEvent::default(); sockets.len().max(1)];

    let active_deadline = Instant::now() + Duration::from_secs(settings.duration_seconds);
    let mut latency_stats = common::LatencyStats::new();
    let mut active_count: u64 = 0;
    let mut phase_done = false;

    while !phase_done {
        match poller.wait(&mut events, -1) {
            Ok(_) => {}
            Err(err) => panic!("poller wait failed: {err}"),
        }
        for socket in &sockets {
            if drain_subscriber(
                socket,
                args.msg_size,
                &mut latency_stats,
                &mut active_count,
                active_deadline,
            ) {
                phase_done = true;
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
    // C returns normally from main; print_result already flushed stdout. Avoid
    // std::process::exit so the flush is not raced.
}
