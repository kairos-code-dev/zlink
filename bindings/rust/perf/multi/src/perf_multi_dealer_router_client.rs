//! DEALER-ROUTER multi client: echo client, N DEALER sockets.
//! Each client sends one message, waits for echo, measures RTT latency.
//! Poller POLLIN for recv drain, POLLOUT for send backpressure.

mod common;

use std::time::{Duration, Instant};
use zlink::*;

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let ctx = Context::new().expect("context");
    let poller = Poller::new().expect("poller");

    let mut sockets: Vec<DealerSocket> = Vec::with_capacity(settings.clients);
    let mut inflight: Vec<bool> = Vec::with_capacity(settings.clients);
    let mut send_pending: Vec<bool> = Vec::with_capacity(settings.clients);

    for i in 0..settings.clients {
        let sock = ctx.dealer_socket().expect("dealer");
        sock.set_send_hwm(settings.hwm).expect("sndhwm");
        sock.set_recv_hwm(settings.hwm).expect("rcvhwm");
        sock.connect(&args.endpoint).expect("connect");
        poller.add_socket(&sock, i, POLLIN).expect("poller add");
        sockets.push(sock);
        inflight.push(false);
        send_pending.push(false);
    }

    std::thread::sleep(Duration::from_millis(500));

    let msg_size = args.msg_size;
    let mut buf = vec![0u8; msg_size.max(common::HEADER_SIZE)];
    let mut seq: u64 = 1;
    let mut latency_stats = common::LatencyStats::new();
    let mut active_count: u64 = 0;
    let n = sockets.len();

    // -- Helper: try send on socket i ----------------------------------------
    let try_send_one = |i: usize, sockets: &[DealerSocket], buf: &mut [u8],
                        seq: &mut u64, phase: u32, poller: &Poller,
                        inflight: &mut [bool], send_pending: &mut [bool]| -> bool {
        if inflight[i] { return true; } // waiting for echo
        common::encode_header(buf, phase, msg_size as u32, *seq);
        let msg = Message::from_bytes(buf).expect("msg");
        match sockets[i].try_send(msg) {
            Ok(SendResult::Sent) => {
                *seq += 1;
                inflight[i] = true;
                send_pending[i] = false;
                true
            }
            _ => {
                send_pending[i] = true;
                let _ = poller.modify_socket(&sockets[i], POLLIN | POLLOUT);
                true
            }
        }
    };

    // -- Phase runner --------------------------------------------------------
    let run_phase = |phase: u32, duration: Duration, sockets: &[DealerSocket],
                     poller: &Poller, buf: &mut [u8], seq: &mut u64,
                     inflight: &mut [bool], send_pending: &mut [bool],
                     count: &mut u64, lat: &mut common::LatencyStats| {
        let deadline = Instant::now() + duration;
        while Instant::now() < deadline {
            // Send on non-inflight sockets
            for i in 0..n {
                if !inflight[i] && !send_pending[i] {
                    try_send_one(i, sockets, buf, seq, phase, poller, inflight, send_pending);
                }
            }

            let remain = (deadline - Instant::now()).as_millis() as i64;
            let wait = remain.min(100).max(1);
            let events = poller.wait_all(n, wait).unwrap_or_default();

            for ev in &events {
                let i = ev.token;
                if i >= sockets.len() {
                    continue;
                }

                if ev.is_readable() {
                    // Drain echo responses
                    loop {
                        match sockets[i].try_recv() {
                            Ok(Some(received)) => {
                                inflight[i] = false;
                                if phase == common::PHASE_ACTIVE {
                                    let data = received.parts()[0].data();
                                    let sent_ts = common::decode_sent_ts(data);
                                    let rtt = common::now_us().saturating_sub(sent_ts) as f64;
                                    lat.record_us(rtt);
                                    *count += 1;
                                }
                            }
                            Ok(None) => break,
                            Err(_) => break,
                        }
                    }
                }
                if ev.is_writable() && send_pending[i] {
                    send_pending[i] = false;
                    let _ = poller.modify_socket(&sockets[i], POLLIN);
                    // Retry send
                    if !inflight[i] {
                        try_send_one(i, sockets, buf, seq, phase, poller, inflight, send_pending);
                    }
                }
            }
        }
    };

    // -- Warmup --------------------------------------------------------------
    run_phase(
        common::PHASE_WARMUP,
        Duration::from_secs(settings.warmup_seconds),
        &sockets, &poller, &mut buf, &mut seq,
        &mut inflight, &mut send_pending,
        &mut 0u64, &mut common::LatencyStats::new(),
    );

    // -- Active --------------------------------------------------------------
    run_phase(
        common::PHASE_ACTIVE,
        Duration::from_secs(settings.duration_seconds),
        &sockets, &poller, &mut buf, &mut seq,
        &mut inflight, &mut send_pending,
        &mut active_count, &mut latency_stats,
    );

    // -- Stop token ----------------------------------------------------------
    if let Some(sock) = sockets.first() {
        let stop_msg = Message::from_bytes(common::STOP_TOKEN).expect("stop");
        let _ = sock.send(stop_msg);
    }

    let stats = latency_stats.finish();
    let final_stats = common::StatsResult { count: active_count, ..stats };
    common::print_result(
        "MULTI_DEALER_ROUTER", &args.transport, msg_size,
        settings.duration_seconds, &final_stats,
    );
}
