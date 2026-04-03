//! Shared perf utilities – metric header, latency stats, phase control.
//!
//! Follows doc/perf/PERF_SINGLE_TEST_POLICY.md:
//!   - ready → warmup(duration) → active(duration)
//!   - callback-only recv model
//!   - metric header in payload for latency measurement

use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

// -- Metric header (32 bytes) ------------------------------------------------
// Layout matches core/perf single metric header:
//   [0..4]   magic     u32 LE  0x53504631 ("SPF1")
//   [4..8]   run_id    u32 LE
//   [8..12]  phase     u32 LE  (0=active, 1=drain, 2=warmup)
//   [12..16] msg_size  u32 LE
//   [16..24] seq       u64 LE
//   [24..32] sent_ts   u64 LE  (microseconds since epoch)

pub const HEADER_SIZE: usize = 32;
pub const MAGIC: u32 = 0x5350_4631; // "SPF1"
pub const PHASE_ACTIVE: u32 = 0;
pub const PHASE_DRAIN: u32 = 1;
pub const PHASE_WARMUP: u32 = 2;

pub fn encode_header(buf: &mut [u8], phase: u32, msg_size: u32, seq: u64) {
    buf[0..4].copy_from_slice(&MAGIC.to_le_bytes());
    buf[4..8].copy_from_slice(&0u32.to_le_bytes()); // run_id
    buf[8..12].copy_from_slice(&phase.to_le_bytes());
    buf[12..16].copy_from_slice(&msg_size.to_le_bytes());
    buf[16..24].copy_from_slice(&seq.to_le_bytes());
    buf[24..32].copy_from_slice(&now_us().to_le_bytes());
}

pub fn decode_sent_ts(data: &[u8]) -> u64 {
    if data.len() < HEADER_SIZE {
        return 0;
    }
    u64::from_le_bytes(data[24..32].try_into().unwrap())
}

pub fn decode_phase(data: &[u8]) -> u32 {
    if data.len() < HEADER_SIZE {
        return PHASE_WARMUP;
    }
    u32::from_le_bytes(data[8..12].try_into().unwrap())
}

pub fn now_us() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_micros() as u64
}

// -- Latency statistics ------------------------------------------------------

pub struct LatencyStats {
    samples: Vec<u64>,
    count: u64,
    sum: u64,
}

impl LatencyStats {
    pub fn new() -> Self {
        Self {
            samples: Vec::with_capacity(1 << 16),
            count: 0,
            sum: 0,
        }
    }

    pub fn record(&mut self, latency_us: u64) {
        self.count += 1;
        self.sum += latency_us;
        if self.samples.len() < 4 * 1024 * 1024 {
            self.samples.push(latency_us);
        }
    }

    pub fn finish(&mut self) -> StatsResult {
        if self.count == 0 {
            return StatsResult::default();
        }
        self.samples.sort_unstable();
        let mean = self.sum as f64 / self.count as f64;
        let p95 = percentile(&self.samples, 0.95);
        let p99 = percentile(&self.samples, 0.99);
        StatsResult {
            count: self.count,
            mean,
            p95,
            p99,
        }
    }
}

fn percentile(sorted: &[u64], p: f64) -> f64 {
    if sorted.is_empty() {
        return 0.0;
    }
    let idx = ((sorted.len() as f64 * p) as usize).min(sorted.len() - 1);
    sorted[idx] as f64
}

#[derive(Default)]
pub struct StatsResult {
    pub count: u64,
    pub mean: f64,
    pub p95: f64,
    pub p99: f64,
}

// -- RESULT output -----------------------------------------------------------

pub fn print_result(
    pattern: &str,
    transport: &str,
    size: usize,
    duration_s: u64,
    stats: &StatsResult,
) {
    let throughput = stats.count as f64 / duration_s as f64;
    let bandwidth = throughput * size as f64 / 1_000_000.0;
    let key = format!("RESULT,current,{pattern},{transport},{size}");
    println!("{key},throughput,{throughput:.2}");
    println!("{key},bandwidth,{bandwidth:.6}");
    println!("{key},latency,{:.2}", stats.mean);
    println!("{key},latency_p95,{:.2}", stats.p95);
    println!("{key},latency_p99,{:.2}", stats.p99);
}

// -- Ready gate / callback handler -------------------------------------------

/// Wait for a monitor CONNECTION_READY event (ready gate).
pub fn wait_monitor_ready(mon: &zlink::SocketMonitor) {
    let deadline = Instant::now() + Duration::from_secs(10);
    loop {
        if Instant::now() > deadline { break; }
        match mon.try_recv() {
            Ok(Some(ev)) if ev.is_connection_ready() || ev.is_accepted() => break,
            Ok(Some(_)) => continue,
            Ok(None) => { std::thread::sleep(Duration::from_millis(10)); }
            Err(_) => break,
        }
    }
}

/// Shared callback body: decode phase, record latency, signal drain.
pub fn handle_recv(
    data: &[u8],
    stats: &std::sync::Mutex<LatencyStats>,
    finished: &std::sync::atomic::AtomicBool,
) {
    let phase = decode_phase(data);
    if phase == PHASE_DRAIN {
        finished.store(true, std::sync::atomic::Ordering::Release);
        return;
    }
    if phase == PHASE_ACTIVE {
        let sent_ts = decode_sent_ts(data);
        let latency = now_us().saturating_sub(sent_ts);
        stats.lock().unwrap().record(latency);
    }
}

// -- Send loop ---------------------------------------------------------------
// Core single perf uses blocking send in the sender thread.
// The sender must not set a send timeout – blocking is the intended behavior
// so that natural backpressure throttles the sender.
// Drain marker uses try_send in a retry loop to avoid deadlock when the
// receiver has already stopped consuming.

use zlink::{Message, SendResult};

/// One-way send loop: warmup → active → drain.
/// `send_fn` performs the blocking send (may be plain or routed).
/// `try_send_fn` performs non-blocking send for drain marker.
pub fn send_loop<S, T>(
    warmup: Duration,
    active: Duration,
    msg_size: usize,
    send_fn: S,
    try_send_fn: T,
) where
    S: Fn(Message),
    T: Fn(Message) -> Result<SendResult, zlink::ZlinkError>,
{
    let mut seq: u64 = 0;
    let mut buf = vec![0u8; msg_size.max(HEADER_SIZE)];

    // Warmup
    let warmup_end = Instant::now() + warmup;
    while Instant::now() < warmup_end {
        encode_header(&mut buf, PHASE_WARMUP, msg_size as u32, seq);
        let msg = Message::from_bytes(&buf).expect("msg");
        send_fn(msg);
        seq += 1;
    }

    // Active
    let active_end = Instant::now() + active;
    while Instant::now() < active_end {
        encode_header(&mut buf, PHASE_ACTIVE, msg_size as u32, seq);
        let msg = Message::from_bytes(&buf).expect("msg");
        send_fn(msg);
        seq += 1;
    }

    // Drain marker – retry with try_send to avoid deadlock
    for _ in 0..200 {
        encode_header(&mut buf, PHASE_DRAIN, msg_size as u32, seq);
        let msg = Message::from_bytes(&buf).expect("msg");
        match try_send_fn(msg) {
            Ok(SendResult::Sent) => return,
            _ => { std::thread::sleep(Duration::from_millis(5)); }
        }
    }
}

/// Common receiver-side finish gate: wait for drain flag or timeout.
pub fn wait_finished(finished: &std::sync::atomic::AtomicBool, warmup: u64, active: u64) {
    let total = Duration::from_secs(warmup + active + 20);
    let end = Instant::now() + total;
    while !finished.load(std::sync::atomic::Ordering::Acquire) && Instant::now() < end {
        std::thread::sleep(Duration::from_millis(10));
    }
}

// -- CLI config --------------------------------------------------------------

pub struct PerfConfig {
    pub pattern: String,
    pub transport: String,
    pub size: usize,
    pub warmup_seconds: u64,
    pub duration_seconds: u64,
}

impl PerfConfig {
    pub fn from_env_and_args() -> Self {
        let args: Vec<String> = std::env::args().collect();
        let mut size = 64;
        let mut warmup = 2u64;
        let mut duration = 5u64;
        let mut transport = "inproc".to_string();
        let mut pattern = "PAIR".to_string();

        let mut i = 1;
        while i < args.len() {
            match args[i].as_str() {
                "--msg-size" if i + 1 < args.len() => {
                    size = args[i + 1].parse().unwrap();
                    i += 2;
                }
                "--warmup" if i + 1 < args.len() => {
                    warmup = args[i + 1].parse().unwrap();
                    i += 2;
                }
                "--duration" if i + 1 < args.len() => {
                    duration = args[i + 1].parse().unwrap();
                    i += 2;
                }
                "--transport" if i + 1 < args.len() => {
                    transport = args[i + 1].clone();
                    i += 2;
                }
                "--pattern" if i + 1 < args.len() => {
                    pattern = args[i + 1].clone();
                    i += 2;
                }
                _ => { i += 1; }
            }
        }

        Self {
            pattern,
            transport,
            size,
            warmup_seconds: warmup,
            duration_seconds: duration,
        }
    }

    pub fn endpoint(&self, suffix: &str) -> String {
        match self.transport.as_str() {
            "inproc" => format!("inproc://perf-{suffix}"),
            "ipc" => format!("ipc:///tmp/zlink-perf-{suffix}-{}", std::process::id()),
            "tcp" => "tcp://127.0.0.1:0".to_string(),
            _ => format!("inproc://perf-{suffix}"),
        }
    }
}
