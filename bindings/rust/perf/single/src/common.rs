//! Shared perf utilities – metric header, latency stats, phase control.
//!
//! Follows doc/perf/PERF_SINGLE_TEST_POLICY.md:
//!   - ready → warmup(duration) → active(duration)
//!   - callback-only recv model
//!   - metric header in payload for latency measurement
#![allow(dead_code)]

use std::sync::{Arc, Condvar, Mutex};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

// -- Metric header (32 bytes) ------------------------------------------------
// Layout matches core/perf single metric header:
//   [0..4]   magic     u32 LE  0x53504631 ("SPF1")
//   [4..8]   run_id    u32 LE
//   [8..12]  phase     u32 LE  (0=active, 1=warmup)
//   [12..16] msg_size  u32 LE
//   [16..24] seq       u64 LE
//   [24..32] sent_ts   u64 LE  (microseconds since epoch)

pub const HEADER_SIZE: usize = 32;
pub const MAGIC: u32 = 0x5350_4631; // "SPF1"
pub const PHASE_ACTIVE: u32 = 0;
pub const PHASE_WARMUP: u32 = 1;

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

#[derive(Default)]
pub struct PhaseResult {
    pub count: u64,
    pub throughput: f64,
    pub bandwidth: f64,
    pub latency_mean: f64,
    pub latency_p95: f64,
    pub latency_p99: f64,
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
        latency_mean: stats.mean,
        latency_p95: stats.p95,
        latency_p99: stats.p99,
    }
}

// -- RESULT output -----------------------------------------------------------

pub fn print_phase_result(key: &str, phase: &PhaseResult) {
    println!("{key},throughput,{:.2}", phase.throughput);
    println!("{key},bandwidth,{:.6}", phase.bandwidth);
    println!("{key},latency,{:.2}", phase.latency_mean);
    println!("{key},latency_p95,{:.2}", phase.latency_p95);
    println!("{key},latency_p99,{:.2}", phase.latency_p99);
}

pub fn print_result(
    pattern: &str,
    transport: &str,
    size: usize,
    duration_s: u64,
    stats: &StatsResult,
) {
    let key = format!("RESULT,current,{pattern},{transport},{size}");
    let phase = build_phase_result(size, duration_s, stats);
    print_phase_result(&key, &phase);
}

pub struct MetricCollector {
    stats: Arc<Mutex<LatencyStats>>,
}

impl MetricCollector {
    pub fn new() -> Self {
        Self {
            stats: Arc::new(Mutex::new(LatencyStats::new())),
        }
    }

    pub fn shared(&self) -> Arc<Mutex<LatencyStats>> {
        Arc::clone(&self.stats)
    }

    pub fn finish(&self) -> StatsResult {
        self.stats.lock().unwrap().finish()
    }
}

#[derive(Clone)]
pub struct CompletionSignal {
    state: Arc<(Mutex<bool>, Condvar)>,
}

impl CompletionSignal {
    pub fn new() -> Self {
        Self {
            state: Arc::new((Mutex::new(false), Condvar::new())),
        }
    }

    pub fn signal_done(&self) {
        let (lock, condvar) = &*self.state;
        let mut done = lock.lock().unwrap();
        *done = true;
        condvar.notify_all();
    }
}

pub struct CompletionGuard {
    signal: CompletionSignal,
}

impl CompletionGuard {
    pub fn new(signal: CompletionSignal) -> Self {
        Self { signal }
    }
}

impl Drop for CompletionGuard {
    fn drop(&mut self) {
        self.signal.signal_done();
    }
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

/// Shared callback body: decode phase and record active latency only.
pub fn handle_recv(data: &[u8], stats: &std::sync::Mutex<LatencyStats>) {
    let phase = decode_phase(data);
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
use zlink::{Message, SendResult};

/// One-way send loop: warmup → active.
/// `send_fn` performs the blocking send (may be plain or routed).
pub fn send_loop<S, T>(
    warmup: Duration,
    active: Duration,
    msg_size: usize,
    send_fn: S,
    _try_send_fn: T,
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

}

/// Common receiver-side finish gate: wait for the sender window plus grace.
pub fn wait_finished(signal: &CompletionSignal, warmup: u64, active: u64) {
    let deadline = Instant::now() + Duration::from_secs(warmup + active + 20);
    let (lock, condvar) = &*signal.state;
    let mut done = lock.lock().unwrap();

    while !*done {
        let now = Instant::now();
        if now >= deadline {
            panic!("single perf sender did not finish before timeout");
        }

        let remaining = deadline.saturating_duration_since(now);
        let (guard, wait_result) = condvar.wait_timeout(done, remaining).unwrap();
        done = guard;
        if wait_result.timed_out() && !*done {
            panic!("single perf sender did not finish before timeout");
        }
    }
}

// -- CLI config --------------------------------------------------------------

pub struct PerfConfig {
    pub pattern: String,
    pub transport: String,
    pub recv_mode: String,
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
        let mut recv_mode =
            std::env::var("PERF_RECV_MODE").unwrap_or_else(|_| "callback".to_string());

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
                "--recv-mode" if i + 1 < args.len() => {
                    recv_mode = args[i + 1].clone();
                    i += 2;
                }
                _ => { i += 1; }
            }
        }

        validate_callback_recv_mode(&recv_mode);

        Self {
            pattern,
            transport,
            recv_mode,
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

fn validate_callback_recv_mode(recv_mode: &str) {
    if recv_mode != "callback" {
        panic!("single perf requires callback recv mode, got `{recv_mode}`");
    }
}
