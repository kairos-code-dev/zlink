# Phase 0.5 -- Profiling Notes

## How to Profile

Profiling should be done with `perf record` / `perf stat` while a benchmark
run is in progress. A typical workflow:

```bash
# Terminal 1 -- start the server under perf
perf record -g -o perf_server.data -- ./stream_server

# Terminal 2 -- start the client (drives load)
cd core/tests/scenario/stream
./run_stream_compare.sh --stack zlink --size 1024 --ccu 1000 --duration 5 --repeats 1

# After the run completes, analyse the recording
perf report -i perf_server.data
```

For quick top-level stats without a full recording:

```bash
perf stat -d -- ./stream_server &
# ... run client ... then Ctrl-C the server
```

## Expected Hotspots

Based on plan analysis, the following are the expected hotspots in
decreasing order of time share:

1. **send/recv syscalls** -- inherent kernel-boundary cost; not directly
   optimizable in user-space code.
2. **memcpy / memmove in the stream engine recv path** -- data is copied
   when frames are extracted from the receive ring buffer. This is the
   target of Phase 2 Commit 2-2 (memmove elimination) and Commit 2-4
   (push_one_frame zero-copy).
3. **malloc / free from async handler allocation** -- every async
   completion currently allocates a new handler object on the heap. This
   is the target of Phase 2 Commit 2-3 (recycling handler pool).

## Priority Order (from the combined plan)

The optimizations should be applied in the following order:

1. **Remove unused members** -- shrink per-connection footprint; trivial
   change, low risk, done first.
2. **memmove elimination** -- replace the memmove inside the recv ring
   buffer with a circular-index scheme.
3. **Handler pool** -- introduce a recycling pool for async completion
   handlers to avoid repeated malloc/free.
4. **Zero-copy push_one_frame (conditional)** -- only proceed if profiling
   confirms that memcpy in the frame-push path is a **top-3 hotspot**.

## Conditional Gate for Zero-Copy (Commit 2-4)

The push_one_frame zero-copy optimisation (Commit 2-4) should **only**
proceed if profiling data confirms that memcpy is among the top-3 hotspots
in the flame graph or `perf report` output. If memcpy does not appear
prominently, the complexity of a zero-copy path is not justified and the
commit should be deferred or skipped.
