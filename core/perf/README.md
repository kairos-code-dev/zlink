# Core PERF Benchmark Scripts

Two shell scripts drive the zlink benchmark suite:

- `run_benchmarks.sh`: single-pattern runner (PAIR, PUBSUB, DEALER_DEALER, etc.)
- `run_benchmarks_multi.sh`: multi-pattern wrapper (multi-socket patterns)

## run_benchmarks.sh

Measures current zlink single-pattern performance. Builds (or reuses) the
project, invokes the Python comparison script, and saves results.

### Options

| Option | Default | Description |
|--------|---------|-------------|
| `--pattern NAME` | `ALL` | Pattern list (comma-separated) or `ALL`. ALL expands to: PAIR, PUBSUB, DEALER_DEALER, DEALER_ROUTER, ROUTER_ROUTER, ROUTER_ROUTER_POLL, GATEWAY, SPOT |
| `--reuse-build` | off | Reuse existing build directory as-is (skip configure/build). Errors if build dir does not exist |
| `--clean-build` | off | Remove build directory, then run a clean configure/build |
| `--build-dir PATH` | auto | Override build directory. Auto-detected as `core/build/<platform>-<arch>` |
| `--output PATH` | — | Tee console output to a file |
| `--save [VERSION]` | off | Save baseline under `results/<suite>/baseline/`. Optional version tag |
| `--results-dir PATH` | `core/perf/results` | Override result root directory |
| `--results-tag NAME` | — | Optional tag appended to result filename |
| `--runs N` | mode-dependent | Iterations per pattern/transport/size. Default: observe=1, trend=3, gate=5 |
| `--duration N` | `5` | Measurement duration in seconds |
| `--hwm N` | — | Set both `PERF_SINGLE_SNDHWM`/`PERF_SINGLE_RCVHWM` fallback via `PERF_SINGLE_HWM` |
| `--send-hwm N` | — | Set `PERF_SINGLE_SNDHWM` (send queue HWM) |
| `--recv-hwm N` | — | Set `PERF_SINGLE_RCVHWM` (receive queue HWM) |
| `--pin-cpu` | off | Pin to CPU core via Linux `taskset` |
| `--io-threads N` | — | Set `PERF_IO_THREADS` for benchmark binaries |
| `--msg-sizes LIST` | — | Comma-separated payload sizes (e.g., `64,1024,65536`) |
| `--transports LIST` | — | Comma-separated transports (e.g., `tcp,tls`) |

### Policy Options

| Option | Default | Description |
|--------|---------|-------------|
| `--mode MODE` | `observe` | `observe`, `trend`, or `gate` |
| `--baseline-file PATH` | — | Explicit baseline file for gate mode |
| `--rolling-n N` | `10` | Rolling baseline window for trend mode |

### Modes

| Mode | Description |
|------|-------------|
| `observe` | Collect metrics only, no baseline comparison. Default runs=1 |
| `trend` | Compare against rolling baseline (warning only). Default runs=3 |
| `gate` | Compare against fixed baseline (warning + fail). Default runs=5 |

### Execution Flow

```
1. Detect platform (linux/macos/windows) and architecture (x64/arm64)
2. Resolve build directory
3. Build using selected mode (default incremental, optional `--reuse-build` / `--clean-build`)
4. Clean up old result directories (>90 days retention)
5. Invoke Python comparison script with:
   - pattern list, build dir, runs, duration
   - mode, rolling-n, baseline-file
   - result-file (tmp output)
6. Environment variables forwarded:
   PERF_IO_THREADS, PERF_MSG_SIZES, PERF_TRANSPORTS,
   PERF_SINGLE_DURATION_SECONDS, PERF_SINGLE_HWM,
   PERF_SINGLE_SNDHWM, PERF_SINGLE_RCVHWM, PERF_NO_AUTOBUILD (reuse mode only)
7. Print total elapsed time on exit
```

### Result Storage

```
results/
  single/
    tmp/          ← always saved (perf_<platform>_<timestamp>[_<tag>].txt)
    report/       ← saved when --save-report (always enabled)
    baseline/     ← saved only with --save [VERSION]
  multi/
    tmp/          ← multi patterns stored here
    report/
    baseline/
```

### Constraints

- MULTI_* patterns are rejected unless `PERF_ALLOW_MULTI=1` is set
- Single and multi patterns cannot be mixed in one run
- Build directory must be inside the repo root

---

## run_benchmarks_multi.sh

Wrapper for multi-socket benchmark patterns. Sets up multi-specific
environment variables and delegates to `run_benchmarks.sh` with
`PERF_ALLOW_MULTI=1`.

### Default Patterns

```
DEALER_DEALER, DEALER_ROUTER, ROUTER_ROUTER,
PUBSUB, GATEWAY, SPOT,
STREAM, STREAM_CALLBACK, STREAM_LEN32BE
```

Default transports: `tcp,tls,ws,wss`

### Options

Shared options (forwarded to `run_benchmarks.sh`):

| Option | Default | Description |
|--------|---------|-------------|
| `--pattern NAME` | all patterns | Pattern list (comma-separated) or `ALL`. `MULTI_` prefix is optional |
| `--reuse-build` | off | Reuse existing build directory as-is (skip configure/build) |
| `--clean-build` | off | Remove build directory, then run a clean configure/build |
| `--build-dir PATH` | auto | Override build directory |
| `--output PATH` | — | Tee results to a file |
| `--save [VER]` | off | Save baseline under `results/multi/baseline/` |
| `--results-dir PATH` | `core/perf/results` | Override results root |
| `--results-tag NAME` | — | Optional tag in filename |
| `--runs N` | `1` | Iterations per configuration |
| `--pin-cpu` | off | Pin CPU core |
| `--io-threads N` | — | I/O worker thread count |
| `--msg-sizes LIST` | — | Comma-separated message sizes |
| `--transports LIST` | `tcp,tls,ws,wss` | Comma-separated transports |

Policy options:

| Option | Default | Description |
|--------|---------|-------------|
| `--mode MODE` | `observe` | `observe`, `trend`, or `gate` |
| `--rolling-n N` | `10` | Rolling baseline window |
| `--baseline-file PATH` | — | Fixed baseline for gate mode |
| `--warn-throughput-pct N` | `10` | Throughput warning drop threshold (%) |
| `--fail-throughput-pct N` | `15` | Throughput fail drop threshold (%) |
| `--warn-latency-pct N` | `10` | Latency warning increase threshold (%) |
| `--fail-latency-pct N` | `15` | Latency fail increase threshold (%) |

Multi-specific options:

| Option | Default | Description |
|--------|---------|-------------|
| `--warmup N` | `3` | Warmup duration (seconds) |
| `--duration N` | `5` | Measurement duration (seconds) |
| `--clients N` | `1000` | Client sockets per pattern |
| `--hwm N` | `1000` | High water mark (send/recv queue depth) |
| `--send-hwm N` | `--hwm` | Send queue high water mark (`PERF_MULTI_SNDHWM`) |
| `--recv-hwm N` | `--hwm` | Receive queue high water mark (`PERF_MULTI_RCVHWM`) |
| `--send-timeout-ms N` | `5000` | Send timeout (ms) |
| `--recv-timeout-ms N` | `5000` | Receive timeout (ms) |
| `--connect-concurrency N` | auto | Concurrent connection count. Auto: 128 (< 10K clients), 1024 (>= 10K) |
| `--drain-ms N` | pattern-specific | Post-measurement drain wait (ms). Default 300 for most patterns, 0 for GATEWAY/SPOT |
| `--transport-transition-ms N` | `3000` | Pause between transport transitions (ms) |
| `--pattern-transition-ms N` | `3000` | Pause between pattern transitions (ms) |
| `--server-ready-timeout-ms N` | `10000` | Wait for server readiness (ms) |
| `--connect-ready-timeout-ms N` | `5000` | Wait for connection readiness (ms) |
| `--monitor-hwm N` | `1000` | Monitor high water mark |
| `--server-shutdown-timeout-ms N` | `5000` | Server shutdown grace period (ms) |
| `--server-bind-port N` | `0` (auto) | Fixed server bind port (0 = OS-assigned) |

### Preflight: nofile Limit Check

Before running each pattern, the script checks the OS file descriptor limit:

```
required = clients × 3 + 4096
```

If the soft limit is insufficient, the script attempts `ulimit -Sn` up to the
hard limit. If it still falls short, the pattern is **skipped** (not failed).
Disable with `PERF_SKIP_NOFILE_CHECK=1`.

### Execution Flow

```
1. Parse CLI options and validate
2. Resolve pattern list (default: all 9 patterns, MULTI_ prefix auto-prepended)
3. For each pattern:
   a. Determine client count (--clients or pattern default 1000)
   b. Preflight nofile limit check → skip on failure
4. Build environment variable array (PERF_* prefix)
5. Invoke run_benchmarks.sh with:
   - PERF_ALLOW_MULTI=1
   - All multi-specific env vars
   - Merged pattern list as single --pattern argument
6. Report skipped and failed patterns on exit
7. Print total elapsed time
```

### Environment Variables

All options can be set via environment variables with `PERF_` prefix.
CLI options take precedence.

| Environment Variable | CLI Equivalent |
|---------------------|----------------|
| `PERF_MULTI_CLIENTS` | `--clients` |
| `PERF_MULTI_HWM` | `--hwm` |
| `PERF_MULTI_SNDHWM` | `--send-hwm` |
| `PERF_MULTI_RCVHWM` | `--recv-hwm` |
| `PERF_MULTI_SNDTIMEO_MS` | `--send-timeout-ms` |
| `PERF_MULTI_RCVTIMEO_MS` | `--recv-timeout-ms` |
| `PERF_MULTI_CONNECT_CONCURRENCY` | `--connect-concurrency` |
| `PERF_MULTI_DRAIN_MS` | `--drain-ms` |
| `PERF_MULTI_WARMUP_SECONDS` | `--warmup` |
| `PERF_MULTI_DURATION_SECONDS` | `--duration` |
| `PERF_MULTI_TRANSPORT_TRANSITION_MS` | `--transport-transition-ms` |
| `PERF_MULTI_PATTERN_TRANSITION_MS` | `--pattern-transition-ms` |
| `PERF_MULTI_SERVER_READY_TIMEOUT_MS` | `--server-ready-timeout-ms` |
| `PERF_MULTI_CONNECT_READY_TIMEOUT_MS` | `--connect-ready-timeout-ms` |
| `PERF_MULTI_MONITOR_HWM` | `--monitor-hwm` |
| `PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS` | `--server-shutdown-timeout-ms` |
| `PERF_MULTI_SERVER_BIND_PORT` | `--server-bind-port` |
| `PERF_MULTI_TIMEOUT_SECONDS` | — (env-only override for client process timeout) |
| `PERF_MULTI_DEFAULT_CLIENTS` | — (default client count when `--clients` not set) |
| `PERF_MULTI_DEFAULT_STREAM_CLIENTS` | — (default client count for STREAM patterns) |
| `PERF_SKIP_NOFILE_CHECK` | — (disable nofile preflight) |
| `PERF_RESULTS_RETENTION_DAYS` | — (old result cleanup threshold, default 90) |

### Examples

Run all multi patterns with defaults:

```bash
./core/perf/run_benchmarks_multi.sh
```

Run specific pattern with custom clients and duration:

```bash
./core/perf/run_benchmarks_multi.sh \
  --pattern STREAM \
  --clients 5000 \
  --duration 10 \
  --transports tcp
```

Gate mode with explicit baseline:

```bash
./core/perf/run_benchmarks_multi.sh \
  --mode gate \
  --baseline-file core/perf/results/multi/baseline/v1.0.txt \
  --fail-throughput-pct 20
```

Save baseline after a full run:

```bash
./core/perf/run_benchmarks_multi.sh --save v2.0
```

Run single-pattern benchmarks:

```bash
./core/perf/run_benchmarks.sh --pattern PAIR --duration 10 --runs 3
```

Run all standard single patterns:

```bash
./core/perf/run_benchmarks.sh
```
