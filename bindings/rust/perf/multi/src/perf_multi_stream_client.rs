//! MULTI_STREAM client: N raw TCP sockets sending payloads, measuring RTT.
//! zlink STREAM client connect is prohibited by policy — use raw TCP.

mod common;

use std::io::{Read, Write};
use std::net::TcpStream;
use std::time::{Duration, Instant};

fn main() {
    let args = common::MultiArgs::parse();
    let settings = common::MultiSettings::from_env();

    let tcp_addr = args.endpoint
        .strip_prefix("tcp://").unwrap_or(&args.endpoint)
        .to_string();

    let mut streams: Vec<TcpStream> = Vec::with_capacity(settings.clients);
    for _ in 0..settings.clients {
        let s = TcpStream::connect(&tcp_addr).expect("tcp connect");
        // Blocking with read timeout — ensures large echoes are fully received
        // while preventing indefinite hang.
        s.set_read_timeout(Some(Duration::from_millis(500))).ok();
        s.set_nodelay(true).ok();
        streams.push(s);
    }

    std::thread::sleep(Duration::from_millis(500));

    let msg_size = args.msg_size;
    let mut buf = vec![0u8; msg_size.max(common::HEADER_SIZE)];
    let mut recv_buf = vec![0u8; msg_size * 2];
    let mut seq: u64 = 1;
    let mut latency_stats = common::LatencyStats::new();
    let mut active_count: u64 = 0;
    let n = streams.len();

    // -- Warmup ---------------------------------------------------------------
    let warmup_end = Instant::now() + Duration::from_secs(settings.warmup_seconds);
    while Instant::now() < warmup_end {
        for i in 0..n {
            common::encode_header(&mut buf, common::PHASE_WARMUP, msg_size as u32, seq);
            seq += 1;
            if streams[i].write_all(&buf).is_ok() {
                let _ = streams[i].flush();
                // Drain echo — read at least msg_size bytes
                drain_echo(&mut streams[i], &mut recv_buf, msg_size);
            }
        }
    }

    // -- Active ---------------------------------------------------------------
    let active_end = Instant::now() + Duration::from_secs(settings.duration_seconds);
    while Instant::now() < active_end {
        for i in 0..n {
            common::encode_header(&mut buf, common::PHASE_ACTIVE, msg_size as u32, seq);
            let t0 = common::now_us();
            seq += 1;
            if streams[i].write_all(&buf).is_ok() {
                let _ = streams[i].flush();
                let got = drain_echo(&mut streams[i], &mut recv_buf, msg_size);
                if got >= msg_size {
                    let rtt = common::now_us().saturating_sub(t0) as f64;
                    latency_stats.record_us(rtt);
                    active_count += 1;
                }
            }
        }
    }

    // -- Stop token -----------------------------------------------------------
    if let Some(s) = streams.first_mut() {
        let _ = s.write_all(common::STOP_TOKEN);
        let _ = s.flush();
    }

    let stats = latency_stats.finish();
    let final_stats = common::StatsResult { count: active_count, ..stats };
    common::print_result("MULTI_STREAM", &args.transport, msg_size,
                         settings.duration_seconds, &final_stats);
}

/// Read from the TCP stream until at least `target` bytes received or timeout.
fn drain_echo(stream: &mut TcpStream, buf: &mut [u8], target: usize) -> usize {
    let mut total = 0;
    while total < target {
        match stream.read(&mut buf[total..]) {
            Ok(0) => break,       // connection closed
            Ok(n) => total += n,
            Err(e) => {
                if e.kind() == std::io::ErrorKind::WouldBlock
                    || e.kind() == std::io::ErrorKind::TimedOut {
                    break;
                }
                break;
            }
        }
    }
    total
}
