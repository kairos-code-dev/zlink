//! DEALER-DEALER multi client: one-way send workload with N client sockets.
//! Poller POLLOUT backpressure: EAGAIN → register POLLOUT, writable → retry.

mod common;

use std::time::{Duration, Instant};
use zlink::*;

struct SocketState {
    index: usize,
    pending: bool,
    pollout_on: bool,
}

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = Context::new().expect("context");
    let poller = Poller::new().expect("poller");

    // Create N client DEALER sockets and connect
    let mut sockets: Vec<DealerSocket> = Vec::with_capacity(settings.clients);
    let mut states: Vec<SocketState> = Vec::with_capacity(settings.clients);

    for i in 0..settings.clients {
        let sock = ctx.dealer_socket().expect("dealer");
        sock.set_send_hwm(settings.hwm).expect("sndhwm");
        sock.connect(&args.endpoint).expect("connect");
        poller.add_socket(&sock, i, 0).expect("poller add"); // start with no events
        states.push(SocketState { index: i, pending: false, pollout_on: false });
        sockets.push(sock);
    }

    // Wait for connections to be ready
    std::thread::sleep(Duration::from_millis(500));

    let warmup_dur = Duration::from_secs(settings.warmup_seconds);
    let active_dur = Duration::from_secs(settings.duration_seconds);
    let msg_size = args.msg_size;
    let mut buf = vec![0u8; msg_size.max(common::HEADER_SIZE)];
    let mut seq: u64 = 1;
    let mut latency_stats = common::LatencyStats::new();

    // -- Warmup phase --------------------------------------------------------
    run_send_phase(
        &sockets, &mut states, &poller,
        &mut buf, &mut seq, msg_size,
        common::PHASE_WARMUP, warmup_dur,
        &mut None,
    );

    // -- Active phase --------------------------------------------------------
    let mut active_count: u64 = 0;
    run_send_phase(
        &sockets, &mut states, &poller,
        &mut buf, &mut seq, msg_size,
        common::PHASE_ACTIVE, active_dur,
        &mut Some((&mut active_count, &mut latency_stats)),
    );

    // -- Send stop token from first socket -----------------------------------
    if let Some(sock) = sockets.first() {
        let stop_msg = Message::from_bytes(common::STOP_TOKEN).expect("stop msg");
        let _ = sock.send(stop_msg);
    }

    // -- Print results -------------------------------------------------------
    let stats = latency_stats.finish();
    let final_stats = common::StatsResult {
        count: active_count,
        ..stats
    };
    common::print_result(
        "MULTI_DEALER_DEALER", &args.transport, msg_size,
        settings.duration_seconds, &final_stats,
    );
}

fn run_send_phase(
    sockets: &[DealerSocket],
    states: &mut [SocketState],
    poller: &Poller,
    buf: &mut [u8],
    seq: &mut u64,
    msg_size: usize,
    phase: u32,
    duration: Duration,
    metrics: &mut Option<(&mut u64, &mut common::LatencyStats)>,
) {
    let deadline = Instant::now() + duration;
    let n = sockets.len();
    let mut index: usize = 0;

    while Instant::now() < deadline {
        let mut progress = false;

        // Round-robin try send on non-pending sockets
        for i in 0..n {
            let si = (index + i) % n;
            if states[si].pending {
                continue;
            }

            common::encode_header(buf, phase, msg_size as u32, *seq);
            let t0 = common::now_us();
            let msg = Message::from_bytes(buf).expect("msg");
            match sockets[si].try_send(msg) {
                Ok(SendResult::Sent) => {
                    *seq += 1;
                    progress = true;
                    if let Some((count, lat)) = metrics.as_mut() {
                        **count += 1;
                        let latency = common::now_us().saturating_sub(t0) as f64;
                        lat.record_us(latency);
                    }
                }
                Ok(SendResult::Backpressured) | Ok(SendResult::NotReady) => {
                    states[si].pending = true;
                    if !states[si].pollout_on {
                        let _ = poller.modify_socket(&sockets[si], POLLOUT);
                        states[si].pollout_on = true;
                    }
                }
                Err(_) => {
                    states[si].pending = true;
                    if !states[si].pollout_on {
                        let _ = poller.modify_socket(&sockets[si], POLLOUT);
                        states[si].pollout_on = true;
                    }
                }
            }
        }
        index += 1;

        // If all sockets made progress, skip poll
        let has_pending = states.iter().any(|s| s.pending);
        if progress || !has_pending {
            continue;
        }

        // Poll for POLLOUT events to resolve backpressure
        let remain_ms = (deadline - Instant::now()).as_millis() as i64;
        let wait_ms = remain_ms.min(100).max(1);
        let events = poller.wait_all(n, wait_ms).unwrap_or_default();

        for ev in &events {
            if !ev.is_writable() {
                continue;
            }
            // Find the state for this socket
            if ev.token >= states.len() {
                continue;
            }
            let st = &mut states[ev.token];
            if st.pending {
                st.pending = false;
                let _ = poller.modify_socket(&sockets[st.index], 0);
                st.pollout_on = false;
            }
        }
    }
}
