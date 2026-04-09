//! Multi perf common utilities.
//! Protocol: server prints "READY,<endpoint>", client receives endpoint via CLI.
//! Stop: client sends STOP_TOKEN, server detects and exits.
#![allow(dead_code)]

#[path = "backpressure.rs"]
pub mod backpressure;

use std::sync::OnceLock;
use std::time::{SystemTime, UNIX_EPOCH};
use zlink::Message;

pub const STOP_TOKEN: &[u8] = b"__zlink_perf_stop__";
pub const HEADER_SIZE: usize = 29;
pub const PHASE_WARMUP: u8 = 0;
pub const PHASE_ACTIVE: u8 = 1;
pub const PHASE_COOLDOWN: u8 = 2;
pub const MAGIC: u32 = 0x5A4C_4E4B; // "ZLNK"

fn process_run_id() -> u32 {
    static RUN_ID: OnceLock<u32> = OnceLock::new();
    *RUN_ID.get_or_init(|| {
        let pid = std::process::id();
        let stamp = now_us() as u32;
        stamp ^ pid.rotate_left(13)
    })
}

pub fn encode_header(buf: &mut [u8], phase: u8, msg_size: u32, seq: u64) {
    buf[0..4].copy_from_slice(&MAGIC.to_le_bytes());
    buf[4..8].copy_from_slice(&process_run_id().to_le_bytes());
    buf[8] = phase;
    buf[9..13].copy_from_slice(&msg_size.to_le_bytes());
    buf[13..21].copy_from_slice(&seq.to_le_bytes());
    buf[21..29].copy_from_slice(&(now_us() as i64).to_le_bytes());
}

pub struct MetricHeader {
    pub magic: u32,
    pub run_id: u32,
    pub phase: u8,
    pub msg_size: u32,
    pub seq: u64,
    pub sent_ts: i64,
}

pub fn decode_header(data: &[u8]) -> Option<MetricHeader> {
    if data.len() < HEADER_SIZE {
        return None;
    }

    Some(MetricHeader {
        magic: u32::from_le_bytes(data[0..4].try_into().unwrap()),
        run_id: u32::from_le_bytes(data[4..8].try_into().unwrap()),
        phase: data[8],
        msg_size: u32::from_le_bytes(data[9..13].try_into().unwrap()),
        seq: u64::from_le_bytes(data[13..21].try_into().unwrap()),
        sent_ts: i64::from_le_bytes(data[21..29].try_into().unwrap()),
    })
}

pub fn decode_phase(data: &[u8]) -> u8 {
    decode_header(data).map(|header| header.phase).unwrap_or(u8::MAX)
}

pub fn decode_msg_size(data: &[u8]) -> u32 {
    decode_header(data).map(|header| header.msg_size).unwrap_or(0)
}

pub fn decode_run_id(data: &[u8]) -> u32 {
    decode_header(data).map(|header| header.run_id).unwrap_or(0)
}

pub fn decode_sent_ts(data: &[u8]) -> i64 {
    decode_header(data).map(|header| header.sent_ts).unwrap_or(0)
}

pub fn message_payload<'a>(parts: &'a [Message]) -> &'a [u8] {
    parts.last().map(|part| part.data()).unwrap_or(&[])
}

pub fn now_us() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_micros() as u64
}

pub fn is_stop_token(data: &[u8]) -> bool {
    data == STOP_TOKEN
}

// -- Latency stats -----------------------------------------------------------

pub struct LatencyStats {
    samples: Vec<f64>,
    count: u64,
    sum: f64,
}

impl LatencyStats {
    pub fn new() -> Self {
        Self { samples: Vec::with_capacity(1 << 16), count: 0, sum: 0.0 }
    }

    pub fn record_us(&mut self, us: f64) {
        self.count += 1;
        self.sum += us;
        if self.samples.len() < 4 * 1024 * 1024 {
            self.samples.push(us);
        }
    }

    pub fn finish(&mut self) -> StatsResult {
        if self.count == 0 { return StatsResult::default(); }
        self.samples.sort_by(|a, b| a.partial_cmp(b).unwrap());
        let mean = self.sum / self.count as f64;
        let p95 = percentile(&self.samples, 0.95);
        let p99 = percentile(&self.samples, 0.99);
        StatsResult { count: self.count, mean_us: mean, p95_us: p95, p99_us: p99 }
    }
}

fn percentile(sorted: &[f64], p: f64) -> f64 {
    if sorted.is_empty() { return 0.0; }
    let idx = ((sorted.len() as f64 * p) as usize).min(sorted.len() - 1);
    sorted[idx]
}

#[derive(Default)]
pub struct StatsResult {
    pub count: u64,
    pub mean_us: f64,
    pub p95_us: f64,
    pub p99_us: f64,
}

#[derive(Default)]
pub struct PhaseResult {
    pub count: u64,
    pub throughput: f64,
    pub bandwidth: f64,
    pub latency_mean_us: f64,
    pub latency_p95_us: f64,
    pub latency_p99_us: f64,
}

pub fn build_phase_result(size: usize, duration_s: u64, stats: &StatsResult) -> PhaseResult {
    let throughput = if duration_s == 0 {
        0.0
    } else {
        stats.count as f64 / duration_s as f64
    };
    let bandwidth = throughput * size as f64 / 1_000_000.0;

    PhaseResult {
        count: stats.count,
        throughput,
        bandwidth,
        latency_mean_us: stats.mean_us,
        latency_p95_us: stats.p95_us,
        latency_p99_us: stats.p99_us,
    }
}

// -- RESULT output -----------------------------------------------------------

pub fn print_phase_result(key: &str, phase: &PhaseResult) {
    println!("{key},throughput,{:.3}", phase.throughput);
    println!("{key},bandwidth,{:.3}", phase.bandwidth);
    println!("{key},latency,{:.3}", phase.latency_mean_us / 1000.0);
    println!("{key},latency_p95,{:.3}", phase.latency_p95_us / 1000.0);
    println!("{key},latency_p99,{:.3}", phase.latency_p99_us / 1000.0);
}

pub fn print_result(pattern: &str, transport: &str, size: usize, duration_s: u64, stats: &StatsResult) {
    let key = format!("RESULT,current,{pattern},{transport},{size}");
    let phase = build_phase_result(size, duration_s, stats);
    print_phase_result(&key, &phase);
}

pub fn print_ready(endpoint: &str) {
    println!("READY,{endpoint}");
    use std::io::Write;
    std::io::stdout().flush().ok();
}

// -- Settings from env -------------------------------------------------------

pub struct MultiSettings {
    pub clients: usize,
    pub duration_seconds: u64,
    pub hwm: i32,
    pub sndtimeo_ms: i32,
    pub rcvtimeo_ms: i32,
}

impl MultiSettings {
    pub fn from_env() -> Self {
        Self {
            clients: env_or("PERF_MULTI_CLIENTS", 100),
            duration_seconds: env_or("PERF_MULTI_DURATION_SECONDS", 5) as u64,
            hwm: env_or("PERF_MULTI_HWM", 100) as i32,
            sndtimeo_ms: env_or("PERF_MULTI_SNDTIMEO_MS", 200) as i32,
            rcvtimeo_ms: env_or("PERF_MULTI_RCVTIMEO_MS", 200) as i32,
        }
    }
}

fn env_or(name: &str, default: usize) -> usize {
    std::env::var(name).ok().and_then(|v| v.parse().ok()).unwrap_or(default)
}

// -- CLI parsing for server/client binaries ----------------------------------

pub struct MultiArgs {
    pub transport: String,
    pub msg_size: usize,
    pub endpoint: String, // client only
}

impl MultiArgs {
    pub fn parse() -> Self {
        let args: Vec<String> = std::env::args().collect();
        let transport = args.get(1).cloned().unwrap_or_else(|| "tcp".into());
        let msg_size: usize = args.get(2).and_then(|s| s.parse().ok()).unwrap_or(64);
        let endpoint = args.get(3).cloned().unwrap_or_default();
        Self { transport, msg_size, endpoint }
    }
}

// -- Wait for stdin STOP (server-side) ---------------------------------------

pub fn wait_for_stop_stdin() {
    use std::io::BufRead;
    let stdin = std::io::stdin();
    for line in stdin.lock().lines() {
        match line {
            Ok(l) if l.trim() == "STOP" || l.trim() == "QUIT" => break,
            Err(_) => break,
            _ => {}
        }
    }
}
