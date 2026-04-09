//! ROUTER-ROUTER multi client: echo client, N ROUTER sockets.
//! Each client sends to server routing-id, receives echo, measures RTT.

mod common;

use std::time::{Duration, Instant};
use zlink::*;

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();
    let server_rid = RoutingId::new(b"perf-rr-server").expect("server rid");

    let ctx = Context::new().expect("context");
    let poller = Poller::new().expect("poller");

    let mut sockets: Vec<RouterSocket> = Vec::with_capacity(settings.clients);
    let mut inflight: Vec<bool> = vec![false; settings.clients];
    let mut send_backpressure: Vec<common::backpressure::SocketBackpressure> =
        (0..settings.clients)
            .map(|_| common::backpressure::SocketBackpressure::new())
            .collect();

    for i in 0..settings.clients {
        let sock = ctx.router_socket().expect("router");
        sock.common_options().set_send_hwm(settings.hwm).expect("sndhwm");
        sock.common_options().set_recv_hwm(settings.hwm).expect("rcvhwm");
        let crid = RoutingId::new(format!("perf-rr-c{i}").as_bytes()).expect("crid");
        sock.set_routing_id(&crid).expect("set rid");
        sock.router_options().set_connect_routing_id(&server_rid).expect("connect rid");
        sock.connect(&args.endpoint).expect("connect");
        poller.add_socket(&sock, i, POLLIN).expect("poller add");
        sockets.push(sock);
    }

    std::thread::sleep(Duration::from_millis(500));

    let msg_size = args.msg_size;
    let mut buf = vec![0u8; msg_size.max(common::HEADER_SIZE)];
    let mut seq: u64 = 1;
    let mut latency_stats = common::LatencyStats::new();
    let mut active_count: u64 = 0;
    let n = sockets.len();

    // -- Phase runner --------------------------------------------------------
    let run_phase = |phase: u8, duration: Duration, sockets: &[RouterSocket],
                     target: &RoutingId, poller: &Poller,
                     buf: &mut [u8], seq: &mut u64,
                     inflight: &mut [bool],
                     send_backpressure: &mut [common::backpressure::SocketBackpressure],
                     count: &mut u64, lat: &mut common::LatencyStats| {
        let deadline = Instant::now() + duration;
        while Instant::now() < deadline {
            for i in 0..n {
                if inflight[i] || send_backpressure[i].is_pending() { continue; }
                common::encode_header(buf, phase, msg_size as u32, *seq);
                let msg = Message::from_bytes(buf).expect("msg");
                match sockets[i].try_send(target, msg) {
                    Ok(SendResult::Sent) => {
                        *seq += 1;
                        inflight[i] = true;
                    }
                    _ => {
                        send_backpressure[i].mark_pending(|| {
                            let _ = poller.modify_socket(&sockets[i], POLLIN | POLLOUT);
                        });
                    }
                }
            }

            let remain = (deadline - Instant::now()).as_millis() as i64;
            let wait = remain.min(50).max(1);
            let events = poller.wait_all(n, wait).unwrap_or_default();

            for ev in &events {
                let i = ev.token;
                if i >= sockets.len() {
                    continue;
                }

                if ev.is_readable() {
                    loop {
                        match sockets[i].try_recv() {
                            Ok(Some(received)) => {
                                inflight[i] = false;
                                if phase == common::PHASE_ACTIVE {
                                    let data = received.parts()[0].data();
                                    let sent_ts_ns = common::decode_sent_ts_ns(data);
                                    let rtt = common::now_ns().saturating_sub(sent_ts_ns.max(0) as u64) as f64;
                                    lat.record_ns(rtt);
                                    *count += 1;
                                }
                            }
                            Ok(None) => break,
                            Err(_) => break,
                        }
                    }
                }
                if ev.is_writable() && send_backpressure[i].is_pending() {
                    send_backpressure[i].clear_pending(|| {
                        let _ = poller.modify_socket(&sockets[i], POLLIN);
                    });
                }
            }
        }
    };

    run_phase(common::PHASE_ACTIVE, Duration::from_secs(settings.duration_seconds),
              &sockets, &server_rid, &poller, &mut buf, &mut seq,
              &mut inflight, &mut send_backpressure,
              &mut active_count, &mut latency_stats);

    for sock in &sockets {
        let stop = Message::from_bytes(common::STOP_TOKEN).expect("stop");
        let _ = sock.send(&server_rid, stop);
    }

    let stats = latency_stats.finish();
    let final_stats = common::StatsResult { count: active_count, ..stats };
    let key = format!(
        "RESULT,current,{},{},{}",
        "MULTI_ROUTER_ROUTER", &args.transport, msg_size
    );
    let phase_result =
        common::build_phase_result(msg_size, settings.duration_seconds, &final_stats);
    common::print_phase_result(&key, &phase_result);
}
