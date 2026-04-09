//! Shared perf utilities - metric header, latency stats, phase control.
//!
//! Follows doc/perf/PERF_SINGLE_TEST_POLICY.md:
//!   - ready -> active(duration)
//!   - recv-only model
//!   - metric header in payload for latency measurement
#![allow(dead_code)]

use std::sync::OnceLock;
use std::sync::{Arc, Condvar, Mutex};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

// -- Metric header (29 bytes) ------------------------------------------------
// Layout matches doc/perf/PERF_POLICY.md:
//   [0..4]   magic      u32 LE  0x5A4C4E4B ("ZLNK")
//   [4..8]   run_id     u32 LE
//   [8]      phase      u8      (0=warmup, 1=active, 2=cooldown)
//   [9..13]  msg_size   u32 LE
//   [13..21] seq        u64 LE
//   [21..29] sent_ts_ns i64 LE  (nanoseconds since epoch)

pub const HEADER_SIZE: usize = 29;
pub const MAGIC: u32 = 0x5A4C_4E4B; // "ZLNK"
pub const PHASE_WARMUP: u8 = 0;
pub const PHASE_ACTIVE: u8 = 1;
pub const PHASE_COOLDOWN: u8 = 2;

fn process_run_id() -> u32 {
    static RUN_ID: OnceLock<u32> = OnceLock::new();
    *RUN_ID.get_or_init(|| {
        let pid = std::process::id();
        let stamp = now_ns() as u32;
        stamp ^ pid.rotate_left(13)
    })
}

pub fn encode_header(buf: &mut [u8], phase: u8, msg_size: u32, seq: u64) {
    buf[0..4].copy_from_slice(&MAGIC.to_le_bytes());
    buf[4..8].copy_from_slice(&process_run_id().to_le_bytes());
    buf[8] = phase;
    buf[9..13].copy_from_slice(&msg_size.to_le_bytes());
    buf[13..21].copy_from_slice(&seq.to_le_bytes());
    buf[21..29].copy_from_slice(&(now_ns() as i64).to_le_bytes());
}

pub fn decode_run_id(data: &[u8]) -> u32 {
    if data.len() < HEADER_SIZE {
        return 0;
    }
    u32::from_le_bytes(data[4..8].try_into().unwrap())
}

pub fn decode_msg_size(data: &[u8]) -> u32 {
    if data.len() < HEADER_SIZE {
        return 0;
    }
    u32::from_le_bytes(data[9..13].try_into().unwrap())
}

pub fn decode_sent_ts_ns(data: &[u8]) -> i64 {
    if data.len() < HEADER_SIZE {
        return 0;
    }
    i64::from_le_bytes(data[21..29].try_into().unwrap())
}

pub fn decode_phase(data: &[u8]) -> u8 {
    if data.len() < HEADER_SIZE {
        return u8::MAX;
    }
    data[8]
}

pub fn is_valid_active_message(data: &[u8], expected_size: usize) -> bool {
    if data.len() < HEADER_SIZE {
        return false;
    }
    decode_phase(data) == PHASE_ACTIVE
        && decode_msg_size(data) as usize == expected_size
        && decode_run_id(data) == process_run_id()
}

pub fn message_payload<'a>(parts: &'a [Message]) -> &'a [u8] {
    parts.last().map(|part| part.data()).unwrap_or(&[])
}

pub fn now_ns() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_nanos() as u64
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

    pub fn record_ns(&mut self, latency_ns: u64) {
        self.count += 1;
        self.sum += latency_ns;
        if self.samples.len() < 4 * 1024 * 1024 {
            self.samples.push(latency_ns);
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
            mean_ns: mean,
            p95_ns: p95,
            p99_ns: p99,
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
    pub mean_ns: f64,
    pub p95_ns: f64,
    pub p99_ns: f64,
}

#[derive(Default)]
pub struct PhaseResult {
    pub count: u64,
    pub throughput: f64,
    pub bandwidth: f64,
    pub latency_mean_ns: f64,
    pub latency_p95_ns: f64,
    pub latency_p99_ns: f64,
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
        latency_mean_ns: stats.mean_ns,
        latency_p95_ns: stats.p95_ns,
        latency_p99_ns: stats.p99_ns,
    }
}

// -- RESULT output -----------------------------------------------------------

pub fn print_phase_result(key: &str, phase: &PhaseResult) {
    println!("{key},throughput,{:.2}", phase.throughput);
    println!("{key},bandwidth,{:.6}", phase.bandwidth);
    println!("{key},latency,{:.3}", phase.latency_mean_ns / 1_000_000.0);
    println!("{key},latency_p95,{:.3}", phase.latency_p95_ns / 1_000_000.0);
    println!("{key},latency_p99,{:.3}", phase.latency_p99_ns / 1_000_000.0);
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

    pub fn is_done(&self) -> bool {
        let (lock, _) = &*self.state;
        *lock.lock().unwrap()
    }

    pub fn wait_timeout(&self, timeout: Duration, label: &str) {
        let deadline = Instant::now() + timeout;
        let (lock, condvar) = &*self.state;
        let mut done = lock.lock().unwrap();

        while !*done {
            let now = Instant::now();
            if now >= deadline {
                panic!("{label} did not finish before timeout");
            }

            let remaining = deadline.saturating_duration_since(now);
            let (guard, wait_result) = condvar.wait_timeout(done, remaining).unwrap();
            done = guard;
            if wait_result.timed_out() && !*done {
                panic!("{label} did not finish before timeout");
            }
        }
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
        if Instant::now() > deadline {
            panic!("single perf connection-ready gate timed out");
        }
        match mon.try_recv() {
            Ok(Some(ev)) if ev.is_connection_ready() => break,
            Ok(Some(_)) => continue,
            Ok(None) => std::thread::yield_now(),
            Err(_) => break,
        }
    }
}

/// Record active-phase latency if the payload matches the expected run.
pub fn handle_recv(data: &[u8], expected_size: usize, stats: &std::sync::Mutex<LatencyStats>) {
    if is_valid_active_message(data, expected_size) {
        let sent_ts_ns = decode_sent_ts_ns(data);
        let latency_ns = (now_ns() as i64).saturating_sub(sent_ts_ns).max(0) as u64;
        stats.lock().unwrap().record_ns(latency_ns);
    }
}

// -- Send loop ---------------------------------------------------------------
// Core single perf uses blocking send in the sender thread.
// The sender must not set a send timeout – blocking is the intended behavior
// so that natural backpressure throttles the sender.
use zlink::{Message, SendResult};

/// One-way send loop: active only.
/// `send_fn` performs the blocking send (may be plain or routed).
pub fn send_loop<S, T>(
    active: Duration,
    msg_size: usize,
    phase: u8,
    send_fn: S,
    _try_send_fn: T,
) where
    S: Fn(Message),
    T: Fn(Message) -> Result<SendResult, zlink::ZlinkError>,
{
    let mut seq: u64 = 0;
    let mut buf = vec![0u8; msg_size.max(HEADER_SIZE)];

    // Active
    let active_end = Instant::now() + active;
    while Instant::now() < active_end {
        encode_header(&mut buf, phase, msg_size as u32, seq);
        let msg = Message::from_bytes(&buf).expect("msg");
        send_fn(msg);
        seq += 1;
    }
}

/// Common receiver-side finish gate: wait for the sender window plus grace.
pub fn wait_finished(signal: &CompletionSignal, active: u64) {
    signal.wait_timeout(Duration::from_secs(active + 20), "single perf sender");
}

// -- CLI config --------------------------------------------------------------

pub struct PerfConfig {
    pub pattern: String,
    pub transport: String,
    pub size: usize,
    pub duration_seconds: u64,
}

impl PerfConfig {
    pub fn from_env_and_args() -> Self {
        let args: Vec<String> = std::env::args().collect();
        let mut size = 64;
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
            duration_seconds: duration,
        }
    }

    pub fn endpoint(&self, suffix: &str) -> String {
        match self.transport.as_str() {
            "inproc" => format!("inproc://perf-{suffix}"),
            "ipc" => "ipc://*".to_string(),
            "ws" => "ws://127.0.0.1:*".to_string(),
            "wss" => "wss://127.0.0.1:*".to_string(),
            "tls" => "tls://127.0.0.1:*".to_string(),
            "tcp" => "tcp://127.0.0.1:*".to_string(),
            _ => format!("inproc://perf-{suffix}"),
        }
    }
}
