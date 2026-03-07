# Core PERF Benchmark Scripts

zlink perf is driven by two shell entrypoints:

- `run_benchmarks.sh`: single-pattern runner
- `run_benchmarks_multi.sh`: multi-pattern wrapper

Both scripts write official results under `core/perf/results/.../report/`.

---

## run_benchmarks.sh

Measures current single-pattern performance (PAIR, PUBSUB, DEALER_DEALER,
DEALER_ROUTER, ROUTER_ROUTER, ROUTER_ROUTER_POLL, GATEWAY, SPOT).

### Options

| Option | Default | Description |
|--------|---------|-------------|
| `--pattern NAME` | `ALL` | Pattern list (comma-separated) or `ALL` |
| `--reuse-build` | off | Reuse existing build directory (skip configure/build) |
| `--clean-build` | off | Remove build directory then clean configure/build |
| `--build-dir PATH` | auto | Override build directory |
| `--output PATH` | — | Tee console output to a file |
| `--results-dir PATH` | `core/perf/results` | Override result root |
| `--results-tag NAME` | — | Optional filename tag |
| `--runs N` | `1` | Iterations per pattern/transport/size |
| `--duration N` | `5` | Active measurement duration (seconds) |
| `--hwm N` | — | Set `PERF_SINGLE_HWM` fallback |
| `--send-hwm N` | — | Set `PERF_SINGLE_SNDHWM` |
| `--recv-hwm N` | — | Set `PERF_SINGLE_RCVHWM` |
| `--sndtimeo N` / `--send-timeout-ms N` | `200` | Set `PERF_SINGLE_SNDTIMEO_MS` |
| `--rcvtimeo N` / `--recv-timeout-ms N` | `200` | Set `PERF_SINGLE_RCVTIMEO_MS` |
| `--pin-cpu` | off | Pin CPU core (Linux taskset) |
| `--io-threads N` | — | Set `PERF_IO_THREADS` |
| `--msg-sizes LIST` | — | Comma-separated sizes |
| `--transports LIST` | — | Comma-separated transports |

Note: `pgm`/`epgm` are currently disabled in single perf.

### Execution model (single)

- One binary process per `pattern/transport/size/run`
- Binary phase: `warmup(count) -> active(duration)`
- Throughput and latency are measured **simultaneously** in active
- Active aggregation uses payload header validation (header-based only)
- No retry/drain phase

### Service poller note

- `GATEWAY`, `RECEIVER`, `SPOT_SUB`, and `SPOT_PUB` perf paths should obtain
  readiness through the service-instance poller APIs.
- Prefer `zlink_poller_add_gateway`, `zlink_poller_add_receiver`,
  `zlink_poller_add_spot_sub`, and `zlink_poller_add_spot_pub`.
- Do not document or reintroduce `SpotNode` as a poller target in perf samples.
- After a service instance is registered with a poller, perf code should keep
  using the service API for send/recv and treat direct internal socket access as
  internal-only/debug-only.

### Result storage

```text
results/
  single/
    report/
      perf_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt
```

---

## run_benchmarks_multi.sh

Wrapper for multi patterns. It normalizes multi options, sets `PERF_ALLOW_MULTI=1`,
and delegates execution to `run_benchmarks.sh`.

### Default patterns

`DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,PUBSUB,GATEWAY,SPOT,STREAM,STREAM_CALLBACK,STREAM_LEN32BE`

### Options

| Option | Default | Description |
|--------|---------|-------------|
| `--pattern NAME` | all defaults | Pattern list (comma-separated). `` prefix optional |
| `--help` | — | Show help |
| `--reuse-build` | off | Reuse existing build directory |
| `--clean-build` | off | Clean build directory first |
| `--results-dir PATH` | `core/perf/results` | Override result root |
| `--results-tag NAME` | — | Optional filename tag |
| `--build-dir PATH` | auto | Override build directory |
| `--output PATH` | — | Tee output to file |
| `--runs N` | `1` | Iterations per configuration |
| `--pin-cpu` | off | Pin CPU core |
| `--io-threads N` | — | Set both server/client io threads |
| `--server-io-threads N` | non-stream=`2`, stream=`4` | Set server io threads |
| `--client-io-threads N` | non-stream=`2`, stream=`4` | Set client io threads |
| `--msg-sizes LIST` | env/default | Comma-separated sizes |
| `--transports LIST` | `tcp,tls,ws,wss` | Comma-separated transports |
| `--warmup N` | `2` | Multi warmup seconds |
| `--duration N` | `5` | Multi active duration seconds |
| `--clients N` | `100` (`stream=10000`) | Clients per pattern |
| `--hwm N` | env/binary default | Set `PERF_HWM` |
| `--send-hwm N` | `--hwm` fallback | Set `PERF_SNDHWM` |
| `--recv-hwm N` | `--hwm` fallback | Set `PERF_RCVHWM` |
| `--sndtimeo N` / `--send-timeout-ms N` | `200` | Set `PERF_SNDTIMEO_MS` |
| `--rcvtimeo N` / `--recv-timeout-ms N` | `200` | Set `PERF_RCVTIMEO_MS` |
| `--connect-concurrency N` | auto | Concurrent connect count |
| `--transport-transition-ms N` | `3000` | Transport cooldown |
| `--pattern-transition-ms N` | `3000` | Pattern cooldown |
| `--server-ready-timeout-ms N` | `10000` | Server ready timeout |
| `--connect-ready-timeout-ms N` | `5000` | Connect-ready timeout |
| `--monitor-hwm N` | `1000` | Monitor HWM |
| `--server-shutdown-timeout-ms N` | `5000` | Server shutdown timeout |
| `--server-bind-port N` | `0` | Fixed server bind port |

### Result storage

```text
results/
  multi/
    report/
      perf_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt
```

### Preflight

- nofile guard (`PERF_SKIP_NOFILE_CHECK=1` to disable)
- memory guard (`PERF_SKIP_MEMORY_CHECK=1` to disable)
  - `PERF_MEMORY_BUDGET_PCT=70` — percent of MemAvailable reserved
  - `PERF_MEMORY_BASE_MB=512` — fixed memory reserve
  - `PERF_MEMORY_PER_CLIENT_KB=1024` — estimated memory per client

---

## Environment variables (common)

| Variable | Meaning |
|----------|---------|
| `PERF_IO_THREADS` | I/O threads |
| `PERF_MSG_SIZES` | Size override |
| `PERF_TRANSPORTS` | Transport override |
| `PERF_RESULTS_DIR` | Results root override |
| `PERF_RESULTS_TAG` | Filename tag |
| `PERF_RESULTS_MAX_FILES` | Max result files per report/ directory (default: 100) |
| `PERF_FAIL_FAST` | Stop early on failure (`1`) |
| `PERF_TASKSET` | CPU pinning (`1`) |

Single-specific variables and full constraints are documented in
`PERF_SINGLE_TEST_POLICY.md`.

Multi-specific variables and full constraints are documented in
`PERF_TEST_POLICY.md`.

---

## Quick examples

Single full run:

```bash
./core/perf/run_benchmarks.sh
```

Single limited run:

```bash
./core/perf/run_benchmarks.sh \
  --pattern PAIR \
  --transports tcp \
  --msg-sizes 64,1024 \
  --runs 3 \
  --duration 5
```

Multi full run:

```bash
./core/perf/run_benchmarks_multi.sh
```

Multi STREAM only:

```bash
./core/perf/run_benchmarks_multi.sh \
  --pattern STREAM \
  --clients 5000 \
  --duration 10 \
  --transports tcp
```
