# Core PERF Benchmark Scripts

zlink perf is driven by two shell entrypoints:

- `run_benchmarks.sh`: single-pattern runner
- `run_benchmarks_multi.sh`: multi-pattern wrapper

Both scripts use `doc/perf/*.md` as the policy source of truth and write
official results under `core/perf/results/.../report/`.

---

## run_benchmarks.sh

Measures current single-pattern performance (PAIR, PUBSUB, DEALER_DEALER,
DEALER_ROUTER, ROUTER_ROUTER, GATEWAY, SPOT).

### Options

| Option | Default | Description |
|--------|---------|-------------|
| `--pattern NAME` | `ALL` | Pattern list (comma-separated) or `ALL` |
| `--reuse-build` | off | Reuse existing build directory (skip configure/build) |
| `--clean-build` | off | Remove build directory then clean configure/build |
| `--build-dir PATH` | `core/build` | Official build directory only |
| `--output PATH` | — | Tee console output to a file |
| `--results-dir PATH` | `core/perf/results` | Override result root |
| `--results-tag NAME` | — | Optional filename tag |
| `--runs N` | `1` | Iterations per pattern/transport/size |
| `--recv MODE` | `recv` | Receive model (`recv` or `callback`) |
| `--duration N` | `5` | Active measurement duration (seconds) |
| `--warmup N` | `2` | Single warmup seconds |
| `--hwm N` | — | Set `PERF_SINGLE_HWM` fallback |
| `--send-hwm N` | — | Set `PERF_SINGLE_SNDHWM` |
| `--recv-hwm N` | — | Set `PERF_SINGLE_RCVHWM` |
| `--sndbuf SIZE` | — | Set `PERF_SINGLE_SNDBUF` (e.g. `64b`, `1k`, `64k`) |
| `--rcvbuf SIZE` | — | Set `PERF_SINGLE_RCVBUF` (e.g. `64b`, `1k`, `64k`) |
| `--sndtimeo N` | `200` | Set `PERF_SINGLE_SNDTIMEO_MS` (milliseconds) |
| `--rcvtimeo N` | `200` | Set `PERF_SINGLE_RCVTIMEO_MS` (milliseconds) |
| `--pin-cpu` | off | Pin CPU core (Linux taskset) |
| `--io-threads N` | — | Set `PERF_IO_THREADS` |
| `--msg-sizes LIST` | — | Comma-separated sizes |
| `--transports LIST` | — | Comma-separated transports |

Note: `pgm`/`epgm` are currently disabled in single perf.

Detailed phase semantics, handshake rules, and mode contracts are defined in
`doc/perf/PERF_SINGLE_TEST_POLICY.md`.

Current single recv-mode support:

- `recv`: `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`,
  `ROUTER_ROUTER`, `GATEWAY`, `SPOT`
- `callback`: `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`,
  `ROUTER_ROUTER`, `GATEWAY`, `SPOT`

Unsupported combinations fail fast instead of silently falling back.

### Result storage

```text
results/
  single/
    report/
      perf_<platform>_<recv_mode>_YYYYMMDD_HHMMSS[_<tag>].txt
```

---

## run_benchmarks_multi.sh

Wrapper for multi patterns. It normalizes multi options, sets `PERF_ALLOW_MULTI=1`,
and delegates execution to `run_benchmarks.sh`.

### Default patterns

`DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,PUBSUB,GATEWAY,SPOT,STREAM`

### Options

| Option | Default | Description |
|--------|---------|-------------|
| `--pattern NAME` | all defaults | Pattern list (comma-separated). `MULTI_` prefix optional |
| `--help` | — | Show help |
| `--reuse-build` | off | Reuse existing build directory |
| `--clean-build` | off | Clean build directory first |
| `--results-dir PATH` | `core/perf/results` | Override result root |
| `--results-tag NAME` | — | Optional filename tag |
| `--build-dir PATH` | `core/build` | Official build directory only |
| `--output PATH` | — | Tee output to file |
| `--runs N` | `1` | Iterations per configuration |
| `--recv MODE` | `recv` | Receive model (`recv` or `callback`) |
| `--pin-cpu` | off | Pin CPU core |
| `--io-threads N` | — | Set both server/client io threads |
| `--server-io-threads N` | non-stream=`2`, stream=`4` | Set server io threads |
| `--client-io-threads N` | non-stream=`2`, stream=`4` | Set client io threads |
| `--msg-sizes LIST` | env/default | Comma-separated sizes |
| `--transports LIST` | `tcp,tls,ws,wss` | Comma-separated transports |
| `--warmup N` | `2` | Multi warmup seconds |
| `--duration N` | `5` | Multi active duration seconds |
| `--clients N` | `100` (`stream=10000`) | Clients per pattern |
| `--hwm N` | env/binary default | Set `PERF_MULTI_HWM` |
| `--send-hwm N` | `--hwm` fallback | Set `PERF_MULTI_SNDHWM` |
| `--recv-hwm N` | `--hwm` fallback | Set `PERF_MULTI_RCVHWM` |
| `--sndtimeo N` / `--send-timeout-ms N` | `200` | Set `PERF_MULTI_SNDTIMEO_MS` |
| `--rcvtimeo N` / `--recv-timeout-ms N` | `200` | Set `PERF_MULTI_RCVTIMEO_MS` |
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
      perf_<platform>_<recv_mode>_YYYYMMDD_HHMMSS[_<tag>].txt
```

Current multi recv-mode support:

- `recv`: `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `PUBSUB`, `STREAM`
- `callback`: `DEALER_DEALER`, `PUBSUB`, `GATEWAY`, `SPOT`, `STREAM`

Unsupported combinations fail fast instead of silently falling back.

### Preflight

- nofile guard (`PERF_SKIP_NOFILE_CHECK=1` to disable)
- memory guard (`PERF_SKIP_MEMORY_CHECK=1` to disable)
  - `PERF_MULTI_MEMORY_BUDGET_PCT=70` — percent of MemAvailable reserved
  - `PERF_MULTI_MEMORY_BASE_MB=512` — fixed memory reserve
  - `PERF_MULTI_MEMORY_PER_CLIENT_KB=1024` — estimated memory per client

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
`PERF_MULTI_TEST_POLICY.md`.

---

## Quick examples

Single full run:

```bash
./core/perf/run_benchmarks.sh --build-dir /home/hep7/project/kairos/zlink/core/build
```

Single limited run:

```bash
./core/perf/run_benchmarks.sh \
  --pattern PAIR \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --transports tcp \
  --msg-sizes 64,1024 \
  --runs 3 \
  --duration 5 \
  --recv recv
```

Multi full run:

```bash
./core/perf/run_benchmarks_multi.sh --build-dir /home/hep7/project/kairos/zlink/core/build
```

Multi STREAM callback:

```bash
./core/perf/run_benchmarks_multi.sh \
  --pattern STREAM \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --clients 5000 \
  --duration 10 \
  --recv callback \
  --transports tcp
```

---

## Refactoring Principles

> Reference: [Core System POSD Refactor Plan](../../doc/plan/refactor/00-core-system-posd-refactor-plan.ko.md),
> [AGENTS.md — Software Design Philosophy](../../AGENTS.md)

The following principles apply when refactoring perf benchmark code and
infrastructure. They derive from the project-wide POSD (A Philosophy of
Software Design) refactoring plan and the repository design philosophy.

### 1. Performance Non-Regression (Priority #1)

- Structural change must never degrade single or multi benchmark baselines.
- Every refactoring phase is gated by a full perf run (`run_benchmarks.sh`,
  `run_benchmarks_multi.sh`) against the recorded baseline before proceeding.
- If a change improves local code quality but regresses throughput/latency,
  reject it.

### 2. Reduce Complexity, Not Just Move Code

- Refactoring must reduce overall system complexity, not relocate it.
- Eliminate shallow wrappers, pass-through layers, and config-flag-driven
  branching that add indirection without adding abstraction.
- Each layer must provide a **different abstraction**, not a thin delegation.

### 3. Deep Modules, Clear Ownership

- Prefer modules with narrow interfaces and rich internals over many small
  functions with wide call surfaces.
- Every resource (socket, context, timer, file descriptor) must have exactly
  **one authoritative close owner** — enforced by structure (RAII, unique
  ownership), not by convention.
- Lifecycle, ownership, and invariants of any component should be
  explainable in a few sentences.

### 4. Information Hiding

- Benchmark binaries should not depend on internal library structure.
- Separate **semantic** concerns (pattern-specific measurement meaning) from
  **mechanism** concerns (process management, result formatting, file I/O).
- Do not expose phase machinery or transport internals to the pattern-level
  measurement code.

### 5. No Retry / No Workaround / No Artificial Flow Control

- No retry logic in scripts or binaries ([PERF_POLICY.md § 8.1](PERF_POLICY.md)).
- No inflight/outstanding limiting options ([PERF_POLICY.md § 8.2](PERF_POLICY.md)).
- No `UNSUPPORTED` misuse to hide failures ([PERF_POLICY.md § 8.4](PERF_POLICY.md)).
- A failure is a real signal — fix the root cause, never mask it.

### 6. Dead Code Cleanup

- Remove unused code, legacy env vars (`PERF_MULTI_ATTEMPTS`, retry-related
  variables, inflight variables), and orphan helpers as part of refactoring.
- Do not leave compatibility shims, `_unused` renames, or `// removed`
  comments.

### 7. Error Prevention by Structure

- Use type system and API design to prevent misuse, not runtime checks or
  policy documentation alone.
- Examples: RAII context guards (`ctx_guard_t`), enum-typed phase states,
  compile-time pattern/transport validation where possible.

### 8. Change Amplification Litmus Test

- After refactoring, adding a new pattern should require only a new source
  file and a transport matrix entry — not changes across shared
  infrastructure.
- Adding a new transport should not require touching pattern-level code.
- If a change in one place forces changes in many others, the abstraction
  boundary is wrong.

### 9. Phase-Gated Progression

- Refactoring proceeds in phases; each phase must pass:
  1. Functional gate — `run_test_lanes.sh` (all test lanes green)
  2. Performance gate — full single + multi perf run, no regression
  3. Hot-path gate — no new lock/alloc/log in measurement path
- Do not begin a new phase until the current phase gates are cleared.
