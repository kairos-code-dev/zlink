//! Multi perf common utilities.
//! Protocol: server prints "READY,<endpoint>", client receives endpoint via CLI.
//! Stop: client sends STOP_TOKEN, server detects and exits.

use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

pub const STOP_TOKEN: &[u8] = b"__zlink_perf_stop__";
pub const HEADER_SIZE: usize = 32;
pub const PHASE_ACTIVE: u32 = 0;
pub const PHASE_DRAIN: u32 = 1;
pub const PHASE_WARMUP: u32 = 2;

pub fn encode_header(buf: &mut [u8], phase: u32, msg_size: u32, seq: u64) {
    let magic: u32 = 0x4d50_4631; // "MPF1"
    buf[0..4].copy_from_slice(&magic.to_le_bytes());
    buf[4..8].copy_from_slice(&0u32.to_le_bytes());
    buf[8..12].copy_from_slice(&phase.to_le_bytes());
    buf[12..16].copy_from_slice(&msg_size.to_le_bytes());
    buf[16..24].copy_from_slice(&seq.to_le_bytes());
    buf[24..32].copy_from_slice(&now_us().to_le_bytes());
}

pub fn decode_phase(data: &[u8]) -> u32 {
    if data.len() < HEADER_SIZE { return PHASE_WARMUP; }
    u32::from_le_bytes(data[8..12].try_into().unwrap())
}

pub fn decode_sent_ts(data: &[u8]) -> u64 {
    if data.len() < HEADER_SIZE { return 0; }
    u64::from_le_bytes(data[24..32].try_into().unwrap())
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

// -- RESULT output -----------------------------------------------------------

pub fn print_result(pattern: &str, transport: &str, size: usize, duration_s: u64, stats: &StatsResult) {
    let throughput = stats.count as f64 / duration_s as f64;
    let bandwidth = throughput * size as f64 / 1_000_000.0;
    let key = format!("RESULT,current,{pattern},{transport},{size}");
    println!("{key},throughput,{throughput:.3}");
    println!("{key},bandwidth,{bandwidth:.3}");
    println!("{key},latency,{:.3}", stats.mean_us / 1000.0);
    println!("{key},latency_p95,{:.3}", stats.p95_us / 1000.0);
    println!("{key},latency_p99,{:.3}", stats.p99_us / 1000.0);
}

pub fn print_ready(endpoint: &str) {
    println!("READY,{endpoint}");
    use std::io::Write;
    std::io::stdout().flush().ok();
}

pub fn print_server_queue_metrics(pattern: &str, transport: &str) {
    let key = format!("RESULT,current,{pattern},{transport},0");
    println!("{key},server_cpu_pct,0.0");
    println!("{key},server_mem_mb,0.0");
}

// -- Settings from env -------------------------------------------------------

pub struct MultiSettings {
    pub clients: usize,
    pub warmup_seconds: u64,
    pub duration_seconds: u64,
    pub hwm: i32,
    pub sndtimeo_ms: i32,
    pub rcvtimeo_ms: i32,
}

impl MultiSettings {
    pub fn from_env() -> Self {
        Self {
            clients: env_or("PERF_MULTI_CLIENTS", 100),
            warmup_seconds: env_or("PERF_SINGLE_WARMUP_SECONDS", 2) as u64,
            duration_seconds: env_or("PERF_SINGLE_DURATION_SECONDS", 5) as u64,
            hwm: env_or("PERF_MULTI_HWM", 100) as i32,
            sndtimeo_ms: env_or("PERF_SINGLE_SNDTIMEO_MS", 200) as i32,
            rcvtimeo_ms: env_or("PERF_SINGLE_RCVTIMEO_MS", 200) as i32,
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
