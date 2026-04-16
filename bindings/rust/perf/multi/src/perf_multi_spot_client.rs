mod common;

use std::thread;
use std::time::{Duration, Instant};
use zlink::*;

const TOPIC: &str = "perf.spot.topic";

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = Context::new().expect("context");
    let mut sockets: Vec<SubSocket> = Vec::with_capacity(settings.clients);

    for _ in 0..settings.clients {
        let sub = ctx.sub_socket().expect("sub");
        sub.common_options().set_recv_hwm(settings.hwm).expect("rcvhwm");
        sub.common_options()
            .set_recv_timeout(Duration::from_millis(1))
            .expect("recv timeout");
        sub.connect(&args.endpoint).expect("connect");
        sub.set_subscription(TOPIC).expect("subscribe");
        sockets.push(sub);
    }

    thread::sleep(Duration::from_millis(500));

    let deadline = Instant::now() + Duration::from_secs(settings.duration_seconds + 2);
    let mut latency_stats = common::LatencyStats::new();
    let mut active_count: u64 = 0;

    while Instant::now() < deadline {
        let mut saw_data = false;
        for sub in &sockets {
            loop {
                match sub.subscribe_with_flags(RecvFlags::DONT_WAIT) {
                    Ok(topic_msg) => {
                        let data = common::message_payload(topic_msg.parts());
                        if common::decode_phase(data) != common::PHASE_ACTIVE {
                            continue;
                        }
                        let sent_ts_ns = common::decode_sent_ts_ns(data);
                        latency_stats.record_ns(
                            common::now_ns().saturating_sub(sent_ts_ns.max(0) as u64) as f64,
                        );
                        active_count += 1;
                        saw_data = true;
                    }
                    Err(err) if err.code == RecvResult::NoData => break,
                    Err(_) => break,
                }
            }
        }
        if !saw_data {
            thread::sleep(Duration::from_millis(1));
        }
    }

    let stats = latency_stats.finish();
    let final_stats = common::StatsResult {
        count: active_count,
        ..stats
    };
    common::print_result(
        "MULTI_SPOT",
        &args.transport,
        args.msg_size,
        settings.duration_seconds,
        &final_stats,
    );
}
