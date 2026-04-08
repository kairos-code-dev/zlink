//! MULTI_SPOT client: N SUB sockets, poller POLLIN drain.

mod common;

use std::time::{Duration, Instant};
use zlink::*;

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = Context::new().expect("context");
    let poller = Poller::new().expect("poller");

    let mut sockets: Vec<SubSocket> = Vec::with_capacity(settings.clients);
    for i in 0..settings.clients {
        let sub = ctx.sub_socket().expect("sub");
        sub.set_recv_hwm(settings.hwm).expect("rcvhwm");
        sub.connect(&args.endpoint).expect("connect");
        sub.set_subscription("").expect("subscribe");
        poller.add_socket(&sub, i, POLLIN).expect("poller add");
        sockets.push(sub);
    }

    std::thread::sleep(Duration::from_millis(500));

    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds + 20);

    let mut latency_stats = common::LatencyStats::new();
    let mut active_count: u64 = 0;
    let mut stop_seen = false;
    let n = sockets.len();

    while !stop_seen && Instant::now() < deadline {
        let remain = (deadline - Instant::now()).as_millis() as i64;
        let wait = remain.min(100).max(1);
        let events = poller.wait_all(n, wait).unwrap_or_default();

        for ev in &events {
            if !ev.is_readable() { continue; }
            let i = ev.token;
            if i >= sockets.len() {
                continue;
            }

            loop {
                match sockets[i].try_subscribe() {
                    Ok(Some(topic_msg)) => {
                        let data = common::callback_payload(topic_msg.parts());
                        if common::is_stop_token(data) { stop_seen = true; break; }
                        let phase = common::decode_phase(data);
                        if phase == common::PHASE_ACTIVE {
                            let sent_ts = common::decode_sent_ts(data);
                            latency_stats.record_us(common::now_us().saturating_sub(sent_ts) as f64);
                            active_count += 1;
                        }
                    }
                    Ok(None) => break,
                    Err(_) => break,
                }
            }
            if stop_seen { break; }
        }
    }

    let stats = latency_stats.finish();
    let final_stats = common::StatsResult { count: active_count, ..stats };
    common::print_result("MULTI_SPOT", &args.transport, args.msg_size,
                         settings.duration_seconds, &final_stats);
}
