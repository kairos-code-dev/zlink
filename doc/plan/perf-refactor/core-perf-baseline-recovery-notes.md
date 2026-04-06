# Core Perf Baseline Recovery Notes

## Scope

- Target: `core/perf` only
- Non-scope: `core` library behavior/API changes
- Goal: recover `core/perf` post-refactor benchmark behavior to within
  baseline error tolerance while preserving perf policy and measurement meaning
- Primary surface under active work:
  - `core/perf/multi/src/perf_multi_spot_client.cpp`
  - `core/perf/multi/src/perf_multi_spot_server.cpp`

## Baseline Contract

- Baseline file for active callback recovery:
  - `core/perf/baseline/perf_linux_callback_20260404_153247.txt`
- Important baseline options confirmed from file:
  - `single io_threads = 1`
  - `multi io_threads = 4`
  - `warmup_seconds = 2`
  - `duration_seconds = 5`
  - `patterns = MULTI_SPOT,MULTI_STREAM`
  - `transports = tcp,tls,ws,wss`
  - `pattern_transition_ms = 3000`
  - `transport_transition_ms = 3000`
- Comparison must use the same transport order as baseline. Running only a
  transport subset gives misleading conclusions for trailing transports.

## Findings So Far

### 1. Single callback regression was perf-surface overhead, not core bug

- `single callback` recovered earlier by tightening phase/notify behavior.
- This confirmed the recovery work should stay inside `core/perf`.

### 2. Multi callback regression is not a single hot-path issue

- Restoring `multi spot client/server` close to commit `59c12181` improved
  behavior compared with the refactored HEAD state.
- That showed the refactor around `multi spot` changed runtime behavior enough
  to affect measured results.

### 3. Baseline comparison is sensitive to runner conditions

- Using only `tls,ws,wss` gave materially different answers from running
  baseline order `tcp,tls,ws,wss`.
- The runner also carried a `transport_transition_ms=5000` environment value in
  some runs. CLI override with `--transport-transition-ms 3000` and
  `--pattern-transition-ms 3000` is required for clean comparison.
- Current `run_benchmarks_multi.sh` also contains callback+spot secure/web auto
  escalation to `transport_transition_ms=5000` unless an explicit CLI override
  is provided.
- That auto escalation must be removed if the perf surface is expected to honor
  the option values recorded in the result file as the true measurement
  contract.

### 4. Result persistence itself needs verification

- At least one completed stability run reported a saved result path on stdout
  but did not leave the corresponding file in `core/perf/results/multi/report/`.
- Until result persistence is consistently verified from disk, baseline
  replacement is not trustworthy.
- Root cause found:
  - `run_comparison.py` prunes old result files after every run.
  - Investigation runs using `results_tag` must be preserved instead of being
    silently deleted.

### 4. Transport-order degradation points to transition cleanup cost/leakage

- `wss` was much worse when it ran after earlier transports than when it was
  measured in isolation.
- This strongly suggests transport-transition cleanup/teardown issues rather
  than pure steady-state send/recv hot-path inefficiency.

### 5. Server-side explicit cleanup helps trailing transports

- Adding explicit perf-surface cleanup on the server before `_Exit()` improved
  the worst trailing transport behavior.
- Observed effect:
  - before server cleanup, full-order run showed roughly
    - `wss throughput -36.14%`
    - `wss latency +213.67%`
  - after server cleanup only, observed output improved to roughly
    - `wss throughput -16.71%`
    - `wss latency +48.38%`
- Conclusion:
  - at least part of the regression comes from server-side transport teardown
    not completing cleanly before the next transport starts.

### 6. Full context termination on the server was too much

- Adding `ctx.force_term()` before server `_Exit()` made the full-order run
  worse again.
- Conclusion:
  - explicit teardown is needed, but forcing full context termination changes
    runtime behavior too much for this perf surface.

### 7. Client-side aggressive cleanup hurts more than it helps

- Cleaning up full client state before `_Exit()` caused transport progression
  problems and hung one verification run after `tcp`.
- Narrowing cleanup to only client control-plane objects also regressed `ws`.
- Conclusion:
  - client process lifetime/exit behavior is part of the measured contract, so
    aggressive pre-exit cleanup on the client changes benchmark behavior.

## Tried And Rejected

### Rejected: broad `perf_common.hpp` rollback

- Rolling back `core/perf/multi/common/perf_common.hpp` toward `59c12181`
  worsened `tls` materially.
- That indicates the whole common refactor is not the direct root cause.
- Recovery should stay narrower around `multi spot` transport transition and
  teardown semantics.

### Rejected: callback-phase hot-path mutex removal

- Removing the per-message phase-update mutex in
  `perf_multi_spot_client.cpp` raised throughput too much and changed measured
  behavior away from baseline.
- This may be “faster”, but it is not baseline-compatible recovery.

### Rejected: client control-plane cleanup before exit

- Full client cleanup caused a stuck run.
- Control-plane-only client cleanup degraded `ws`.
- Keep client exit behavior close to the restored pre-refactor shape for now.

### Rejected: server context force termination before exit

- `ctx.force_term()` on the server regressed the full-order run compared with
  `server cleanup only`.
- Keep the narrower server resource cleanup, but do not force full context
  termination.

### Rejected: server cleanup before exit

- A same-condition A/B was run with `run_benchmarks_multi.sh` using
  `MULTI_SPOT,MULTI_STREAM`, `tcp,tls,ws,wss`, `64B`, `warmup=2`,
  `duration=5`, `io_threads=4`, `pattern_transition_ms=3000`, and
  `transport_transition_ms=3000`.
- `cleanup on` result:
  `core/perf/results/multi/report/perf_linux_callback_20260406_215438_cleanup_on_m.txt`
- `cleanup off` result:
  `core/perf/results/multi/report/perf_linux_callback_20260406_215739_cleanup_off_m.txt`
- `cleanup off` improved `MULTI_SPOT tcp/ws/wss` materially and removed the
  worst `wss` latency inflation, while `tls` stayed roughly unchanged.
- Because the cleanup path runs after measurement but before process exit, it
  changes process-lifetime and teardown behavior enough to perturb the next
  benchmark case.
- The explicit server cleanup path should therefore be removed from the perf
  surface.

### Confirmed: `MULTI_STREAM` echo poll slicing changed runtime behavior

- Baseline commit `59c12181` passed the full remaining phase time into the
  echo service loop.
- The refactored `perf_multi_echo_policy.hpp` changed that to
  `min(remaining_ms, 10ms)` slices via `echo_poll_slice_ms()`.
- That adds frequent extra wakeups inside the active request/response flow and
  is a plausible direct source of `MULTI_STREAM tls/ws/wss` degradation.
- Recovery should restore the old `remaining_ms` service cadence by default.

### Confirmed: current `MULTI_STREAM` secure/web numbers are stable after the
slice revert

- Repeated `MULTI_STREAM` callback runs after the slice revert:
  - `core/perf/results/multi/report/perf_linux_callback_20260406_220429_stream_echo_slice_revert_o.txt`
  - `core/perf/results/multi/report/perf_linux_callback_20260406_220620_stream_echo_slice_revert_p.txt`
- Repeat deltas were mostly within about `5%`:
  - `tcp throughput -2.48%, latency +2.20%`
  - `tls throughput -1.10%, latency +1.08%`
  - `ws throughput -5.08%, latency +5.46%`
  - `wss throughput +0.87%, latency -0.95%`
- That means the current `MULTI_STREAM` secure/web surface is now reasonably
  repeatable, even though it still differs from the old baseline file.

### Confirmed: `MULTI_SPOT wss` no longer looks like a transition-only problem

- `MULTI_SPOT wss` remained materially below the old baseline even when run as
  the only transport:
  - `core/perf/results/multi/report/perf_linux_callback_20260406_220915_spot_wss_only_s.txt`
- That isolates the gap away from `tcp -> tls -> ws -> wss` transition order.
- Combined with the fact that `perf_multi_spot_client.cpp` and
  `perf_multi_spot_server.cpp` are effectively identical to baseline commit
  `59c12181` apart from include path changes, the remaining gap is no longer a
  credible `core/perf` spot hot-path regression candidate.

## Current Best Direction

- Keep restored `multi spot client/server` behavior near the older baseline-like
  implementation.
- Do not add explicit pre-exit cleanup on the `multi spot` server perf surface.
- Avoid broad common rollback.
- Avoid client-side cleanup that changes timing/lifetime semantics.
- Keep `MULTI_STREAM` echo servicing on the baseline-style remaining-time poll
  cadence, not a fixed short slice.
- Treat the old multi secure/web baseline as suspect once the current surface is
  shown to be repeatable under the documented runner contract.
- Rebaseline from the current runner contract after documenting the recovered
  perf-surface fixes and the repeatability evidence.

## Open Hypotheses

1. Active-phase messages already accepted into callback/poller queues may still
   be lost or delayed across the stop boundary.
2. The old baseline file may reflect a no-longer-reproducible environment or
   runner state for secure/web transports.
3. Some remaining secure/web gap may come from transport/runtime differences
   outside the now-restored perf-surface code path.

## Usage For Bindings Work

- When bindings perf recovery shows “later transports are much worse than
  isolated transports”, check transition cleanup first, not just callback
  hot-path code.
- Always compare with the same transport order and transition settings recorded
  in baseline.
- Do not assume “faster than baseline” is acceptable; the target here is
  baseline-compatible measurement behavior, not raw throughput maximization.
