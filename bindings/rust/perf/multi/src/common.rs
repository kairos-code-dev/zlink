#![allow(dead_code)]

//! Multi perf common utilities.

#[path = "backpressure.rs"]
pub mod backpressure;

use std::fs;
use std::path::Path;
use std::time::{SystemTime, UNIX_EPOCH};
use zlink::Message;
use zlink::{
    DealerSocket, PairSocket, PubSocket, RouterSocket, StreamSocket, SubSocket, ZlinkError,
};

pub const STOP_TOKEN: &[u8] = b"__zlink_perf_stop__";
pub const HEADER_SIZE: usize = 29;
pub const PHASE_WARMUP: u8 = 0;
pub const PHASE_ACTIVE: u8 = 1;
pub const PHASE_COOLDOWN: u8 = 2;
pub const MAGIC: u32 = 0x5A4C_4E4B; // "ZLNK"
pub const BENCHMARK_RUN_ID: u32 = 1;

pub fn encode_header(buf: &mut [u8], phase: u8, msg_size: u32, seq: u64) {
    buf[0..4].copy_from_slice(&MAGIC.to_le_bytes());
    buf[4..8].copy_from_slice(&BENCHMARK_RUN_ID.to_le_bytes());
    buf[8] = phase;
    buf[9..13].copy_from_slice(&msg_size.to_le_bytes());
    buf[13..21].copy_from_slice(&seq.to_le_bytes());
    buf[21..29].copy_from_slice(&(now_ns() as i64).to_le_bytes());
}

pub fn decode_magic(data: &[u8]) -> u32 {
    if data.len() < HEADER_SIZE {
        return 0;
    }
    u32::from_le_bytes(data[0..4].try_into().unwrap())
}

pub fn decode_phase(data: &[u8]) -> u8 {
    if data.len() < HEADER_SIZE {
        return u8::MAX;
    }
    data[8]
}

pub fn decode_msg_size(data: &[u8]) -> u32 {
    if data.len() < HEADER_SIZE {
        return 0;
    }
    u32::from_le_bytes(data[9..13].try_into().unwrap())
}

pub fn decode_run_id(data: &[u8]) -> u32 {
    if data.len() < HEADER_SIZE {
        return 0;
    }
    u32::from_le_bytes(data[4..8].try_into().unwrap())
}

pub fn decode_sent_ts_ns(data: &[u8]) -> i64 {
    if data.len() < HEADER_SIZE {
        return 0;
    }
    i64::from_le_bytes(data[21..29].try_into().unwrap())
}

pub fn message_payload<'a>(parts: &'a [Message]) -> &'a [u8] {
    parts.last().map(|part| part.as_bytes()).unwrap_or(&[])
}

pub fn now_ns() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos() as u64
}

pub struct TlsPaths {
    pub cert: String,
    pub key: String,
    pub ca: String,
}

pub struct TlsPem {
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

impl_raw_tls_socket!(PairSocket, PubSocket, DealerSocket, RouterSocket, StreamSocket, SubSocket);

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

pub fn load_tls_pem(tls: &TlsPaths) -> TlsPem {
    TlsPem {
        cert: fs::read_to_string(&tls.cert).expect("read tls cert"),
        key: fs::read_to_string(&tls.key).expect("read tls key"),
        ca: fs::read_to_string(&tls.ca).expect("read tls ca"),
    }
}

pub fn emit_unsupported(pattern: &str, transport: &str, reason: &str) {
    let _ = reason;
    println!("UNSUPPORTED,rust,{pattern},{transport}");
    use std::io::Write;
    std::io::stdout().flush().ok();
}

pub fn is_transport_unsupported_error(err: &ZlinkError) -> bool {
    matches!(
        err.internal_errno(),
        libc::EPERM | libc::EACCES | libc::ENOTSUP
    )
}

pub fn handle_transport_setup_error<E>(
    pattern: &str,
    transport: &str,
    stage: &str,
    err: E,
) -> bool
where
    E: Into<ZlinkError> + Copy,
{
    let err = err.into();
    if is_transport_unsupported_error(&err) {
        emit_unsupported(
            pattern,
            transport,
            &format!("{stage}_errno_{}", err.internal_errno()),
        );
        return true;
    }
    false
}

pub fn is_stop_token(data: &[u8]) -> bool {
    data == STOP_TOKEN
}

pub fn is_valid_message(data: &[u8], expected_size: usize) -> bool {
    data.len() >= HEADER_SIZE
        && decode_magic(data) == MAGIC
        && decode_run_id(data) == BENCHMARK_RUN_ID
        && decode_msg_size(data) as usize == expected_size
}

pub fn is_valid_active_message(data: &[u8], expected_size: usize) -> bool {
    is_valid_message(data, expected_size) && decode_phase(data) == PHASE_ACTIVE
}

// -- Latency stats -----------------------------------------------------------

pub struct LatencyStats {
    samples: Vec<f64>,
    count: u64,
    sum: f64,
}

impl LatencyStats {
    pub fn new() -> Self {
        Self {
            samples: Vec::with_capacity(resolve_multi_latency_sample_cap().min(1 << 16)),
            count: 0,
            sum: 0.0,
        }
    }

    pub fn record_ns(&mut self, ns: f64) {
        self.count += 1;
        self.sum += ns;
        if self.samples.len() < resolve_multi_latency_sample_cap() {
            self.samples.push(ns);
        }
    }

    pub fn finish(&mut self) -> StatsResult {
        if self.count == 0 { return StatsResult::default(); }
        self.samples.sort_by(|a, b| a.partial_cmp(b).unwrap());
        let mean = self.sum / self.count as f64;
        let p95 = percentile(&self.samples, 0.95);
        let p99 = percentile(&self.samples, 0.99);
        StatsResult { count: self.count, mean_ns: mean, p95_ns: p95, p99_ns: p99 }
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

fn bandwidth_multiplier(pattern: &str) -> f64 {
    match pattern {
        "MULTI_DEALER_ROUTER" | "MULTI_ROUTER_ROUTER" | "MULTI_STREAM" | "MULTI_SPOT_REQREP" => {
            2.0
        }
        _ => 1.0,
    }
}

pub fn build_phase_result(
    pattern: &str,
    size: usize,
    duration_s: u64,
    stats: &StatsResult,
) -> PhaseResult {
    let throughput = if duration_s == 0 {
        0.0
    } else {
        stats.count as f64 / duration_s as f64
    };
    let bandwidth = throughput * size as f64 * bandwidth_multiplier(pattern) / 1_000_000.0;

    PhaseResult { throughput, bandwidth, latency_mean_ns: stats.mean_ns, latency_p95_ns: stats.p95_ns, latency_p99_ns: stats.p99_ns }
}

// -- RESULT output -----------------------------------------------------------

pub fn print_phase_result(key: &str, phase: &PhaseResult) {
    println!("{key},throughput,{:.3}", phase.throughput);
    println!("{key},bandwidth,{:.3}", phase.bandwidth);
    println!("{key},latency,{:.3}", phase.latency_mean_ns / 1_000_000.0);
    println!("{key},latency_p95,{:.3}", phase.latency_p95_ns / 1_000_000.0);
    println!("{key},latency_p99,{:.3}", phase.latency_p99_ns / 1_000_000.0);
}

pub fn print_result(pattern: &str, transport: &str, size: usize, duration_s: u64, stats: &StatsResult) {
    let key = format!("RESULT,current,{pattern},{transport},{size}");
    let phase = build_phase_result(pattern, size, duration_s, stats);
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
    pub send_hwm: i32,
    pub recv_hwm: i32,
    pub send_timeout_ms: u64,
    pub recv_timeout_ms: u64,
}

impl MultiSettings {
    pub fn from_env() -> Self {
        Self {
            clients: env_or("PERF_MULTI_CLIENTS", 100),
            duration_seconds: env_or("PERF_MULTI_DURATION_SECONDS", 5) as u64,
            send_hwm: env_or_i32("PERF_MULTI_SNDHWM", env_or_i32("PERF_MULTI_HWM", 1000)),
            recv_hwm: env_or_i32("PERF_MULTI_RCVHWM", env_or_i32("PERF_MULTI_HWM", 1000)),
            send_timeout_ms: env_or("PERF_MULTI_SNDTIMEO_MS", 200) as u64,
            recv_timeout_ms: env_or("PERF_MULTI_RCVTIMEO_MS", 200) as u64,
        }
    }
}

fn env_or(name: &str, default: usize) -> usize {
    std::env::var(name).ok().and_then(|v| v.parse().ok()).unwrap_or(default)
}

fn env_or_i32(name: &str, default: i32) -> i32 {
    std::env::var(name)
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(default)
}

fn resolve_multi_latency_sample_cap() -> usize {
    env_or("PERF_MULTI_LATENCY_SAMPLE_CAP", 200_000)
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
