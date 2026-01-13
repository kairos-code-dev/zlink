## Background
The ASIO-only write path refactor (batched writes, lazy compaction, handshake zerocopy, etc.) in `feature/perf-optimization` was developed with extensive test coverage and a full set of benchmarks. Once merged back to `main`, multiple `benchwithzmq` runs (20 iterations per configuration on CPU 0) show that TCP/IPC small-message throughput is **still 15‑40% worse than libzmq** and latency is several orders of magnitude higher, while only inproc/very large transfers approach parity (see `benchwithzmq/benchmark_result.txt` for the latest run). The optimizations therefore *did not deliver* the expected wins on latency-sensitive workloads. We now need to step back, understand the regression causes, and sketch a new follow‑up rather than continuing to layer more complexity on the current path.

## Diagnosis
1. **Batching backfires for short packets** – the batch timer/counters show that `zlink` still frequently flushes due to either timeout or count thresholds, and when the send engine waits for a batch the per-packet latency balloons. In addition, zero-copy is kicked in only for messages ≥8 KB, so short messages always go through the encoder + memcpy path that the batching targets.
2. **Socket/timer interplay** – the TCP/IPC benchmarks show unchanged latency even with batching disabled via the feature flag, suggesting that the baseline loop (io_context + async write) still incurs more queuing than libzmq’s straight `send`. The debug counters and UBSAN trace do not show leaks, so the issue is structural in the engine/transport.
3. **Handshake/transport overhead** – the additional runtime flags and zero-copy handshake path did not reduce latency because the handshake buffer optimization only applies per connection and the benchmarks reuse connections repeatedly; there is no benefit for the steady-state small-message flows being measured.

## Follow-up Proposal
1. **Measure pre- vs post-batching latency/throughput with instrumentation**  
   - Add microbench tests that run duplex short-message flows within the `tests/bench` harness and log latency with and without batching.  
   - Use `zmq_debug` counters to verify the path messages take (batching + memcpy).  
2. **Simplify the short-path write stack**  
   - Consider bypassing the encoder/`_write_buffer` for contention scenarios: fall back to a plain `async_write` once `_write_batching_enabled` and `_write_batch_max_messages` are not providing wins.  
   - Evaluate whether the batching timer threshold (5 ms by default) should depend on `_options.out_batch_size` or be set to zero for latency critical sockets.  
3. **Expose runtime knobs per socket**  
   - Allow users to disable batching/compaction per socket (not just via env var) and document how to revert to a single-message path.  
   - Provide telemetry in `zmq_debug` to show how often batching actually coalesces short writes; if the counter stays zero, it may be better to deactivate the feature altogether.
4. **Re-benchmark after tuning**  
   - Run the pinned 20-iteration benchmarks again after each change (with `taskset -c 0 ... --runs 20`) so we can see whether the latency/throughput gaps close.  
   - Record results under `benchwithzmq/zlink_BENCHMARK_RESULTS.md` for comparisons so we know when the regression is resolved.

## Documentation
Move the detailed notes above into the action plan file under `doc/plan/FAILED_PERFORMANCE_WORK/RETRY_PERF_PLAN.md`. Reference the benchmark output and `zmq_debug` counters when filing any follow-up tickets or PR narratives so reviewers understand the regression context.
