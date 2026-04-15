//! Shared perf utilities - metric header, latency stats, phase control.
//!
//! Follows doc/perf/PERF_SINGLE_TEST_POLICY.md:
//!   - ready -> active(duration)
//!   - recv-only model
//!   - metric header in payload for latency measurement

use std::sync::OnceLock;
use std::sync::{Arc, Condvar, Mutex};
use std::path::Path;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use zlink::{DealerSocket, PairSocket, PubSocket, RouterSocket, SubSocket, ZlinkError};

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
pub const PHASE_ACTIVE: u8 = 1;

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
    parts.last().map(|part| part.as_bytes()).unwrap_or(&[])
}

pub fn now_ns() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_nanos() as u64
}

pub struct TlsPaths {
    pub cert: String,
    pub key: String,
    pub ca: String,
}

pub trait RawTlsSocket {
    fn set_tls_cert(&self, cert: &str) -> Result<(), ZlinkError>;
    fn set_tls_key(&self, key: &str) -> Result<(), ZlinkError>;
    fn set_tls_ca(&self, ca: &str) -> Result<(), ZlinkError>;
    fn set_tls_hostname(&self, hostname: &str) -> Result<(), ZlinkError>;
    fn set_tls_trust_system(&self, trust_system: bool) -> Result<(), ZlinkError>;
}

macro_rules! impl_raw_tls_socket {
    ($($ty:ty),+ $(,)?) => {
        $(
            impl RawTlsSocket for $ty {
                fn set_tls_cert(&self, cert: &str) -> Result<(), ZlinkError> {
                    Ok(<$ty>::set_tls_cert(self, cert)?)
                }
                fn set_tls_key(&self, key: &str) -> Result<(), ZlinkError> {
                    Ok(<$ty>::set_tls_key(self, key)?)
                }
                fn set_tls_ca(&self, ca: &str) -> Result<(), ZlinkError> {
                    Ok(<$ty>::set_tls_ca(self, ca)?)
                }
                fn set_tls_hostname(&self, hostname: &str) -> Result<(), ZlinkError> {
                    Ok(<$ty>::set_tls_hostname(self, hostname)?)
                }
                fn set_tls_trust_system(&self, trust_system: bool) -> Result<(), ZlinkError> {
                    Ok(<$ty>::set_tls_trust_system(self, trust_system)?)
                }
            }
        )+
    };
}

impl_raw_tls_socket!(PairSocket, PubSocket, DealerSocket, RouterSocket, SubSocket);

fn resolve_perf_tls_paths_from(start: &Path) -> Option<TlsPaths> {
    let mut cur = if start.is_file() {
        start.parent()?.to_path_buf()
    } else {
        start.to_path_buf()
    };

    loop {
        for candidate in [
            cur.join("bindings").join("cpp").join("tests").join("certs").join("gen"),
            cur.join("bindings").join("rust").join("tests").join("certs").join("gen"),
            cur.join("bindings").join("java").join("tests").join("certs"),
            cur.join("bindings").join("dotnet").join("tests").join("certs"),
            cur.join("tests").join("certs").join("gen"),
        ] {
            if candidate.join("server.crt").is_file()
                && candidate.join("server.key").is_file()
                && candidate.join("ca.crt").is_file()
            {
                return Some(TlsPaths {
                    cert: candidate.join("server.crt").to_string_lossy().into_owned(),
                    key: candidate.join("server.key").to_string_lossy().into_owned(),
                    ca: candidate.join("ca.crt").to_string_lossy().into_owned(),
                });
            }
        }

        let parent = match cur.parent() {
            Some(parent) => parent.to_path_buf(),
            None => break,
        };
        if parent == cur {
            break;
        }
        cur = parent;
    }

    None
}

pub fn resolve_perf_tls_paths() -> Option<TlsPaths> {
    if let Ok(cwd) = std::env::current_dir() {
        if let Some(paths) = resolve_perf_tls_paths_from(&cwd) {
            return Some(paths);
        }
    }

    if let Ok(exe) = std::env::current_exe() {
        if let Some(paths) = resolve_perf_tls_paths_from(&exe) {
            return Some(paths);
        }
    }

    None
}

pub fn setup_raw_tls_server<S: RawTlsSocket>(socket: &S, tls: &TlsPaths) -> Result<(), ZlinkError> {
    socket.set_tls_cert(&tls.cert)?;
    socket.set_tls_key(&tls.key)?;
    Ok(())
}

pub fn setup_raw_tls_client<S: RawTlsSocket>(socket: &S, tls: &TlsPaths) -> Result<(), ZlinkError> {
    socket.set_tls_ca(&tls.ca)?;
    socket.set_tls_hostname("localhost")?;
    socket.set_tls_trust_system(false)?;
    Ok(())
}

#[allow(dead_code)]
/// Wait for a monitor CONNECTION_READY event (ready gate).
pub fn wait_monitor_ready(mon: &zlink::SocketMonitor) {
    let deadline = Instant::now() + Duration::from_secs(10);
    loop {
        if Instant::now() > deadline {
            panic!("single perf connection-ready gate timed out");
        }
        match mon.recv() {
            Ok(ev) if ev.is_connection_ready() => break,
            Ok(_) => continue,
            Err(_) => break,
        }
    }
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

    PhaseResult { throughput, bandwidth, latency_mean_ns: stats.mean_ns, latency_p95_ns: stats.p95_ns, latency_p99_ns: stats.p99_ns }
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
use zlink::Message;

/// One-way send loop: active only.
/// `send_fn` performs the blocking send (may be plain or routed).
pub fn send_loop<S>(
    active: Duration,
    msg_size: usize,
    phase: u8,
    send_fn: S,
) where
    S: Fn(Message),
{
    let mut seq: u64 = 0;
    let mut buf = vec![0u8; msg_size.max(HEADER_SIZE)];

    // Active
    let active_end = Instant::now() + active;
    while Instant::now() < active_end {
        encode_header(&mut buf, phase, msg_size as u32, seq);
        let msg = Message::copy_from(&buf).expect("msg");
        send_fn(msg);
        seq += 1;
    }
}

// -- CLI config --------------------------------------------------------------

pub struct PerfConfig {
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
                    i += 2;
                }
                _ => { i += 1; }
            }
        }

        Self { transport, size, duration_seconds: duration }
    }

    #[allow(dead_code)]
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
