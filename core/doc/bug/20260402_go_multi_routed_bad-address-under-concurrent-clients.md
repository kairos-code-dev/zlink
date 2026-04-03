# Go Multi Routed Perf Bad Address Under Concurrent Clients

## Summary

`bindings/go` multi routed perf (`MULTI_DEALER_ROUTER`, `MULTI_ROUTER_ROUTER`) fails under concurrent client counts before comparable recv/callback full runs can complete.
The failure surfaces from core/native recv paths as `Bad address` or `native error: Success`, so the Go bindings perf work is currently blocked on a core-side investigation.

## Baseline / Goal

- recv baseline: `/home/hep7/project/kairos/zlink/core/perf/baseline/perf_linux_recv_20260331_033330.txt`
- callback baseline: `/home/hep7/project/kairos/zlink/core/perf/baseline/perf_linux_callback_20260331_044331.txt`
- active binding target: `go >= 0.85`
- execution guide: `/home/hep7/project/kairos/zlink/core/tools/bindings-perf/bindings-perf-execution-guide.ko.md`

## Reproduction

From `/home/hep7/project/kairos/zlink/bindings/go/perf` after building `core/build/` and passing `go test ./...` in `/home/hep7/project/kairos/zlink/bindings/go`:

```bash
./run_benchmarks_multi.sh \
  --pattern MULTI_DEALER_ROUTER \
  --recv recv \
  --msg-sizes 64 \
  --warmup 1 \
  --duration 1 \
  --clients 10 \
  --results-tag codex_repro_dealer_router_clients10_tryrecv
```

Observed:

```text
multi dealer/router recv: native error (14): Bad address
exit status 1
```

Also reproducible with:

```bash
./run_benchmarks_multi.sh \
  --pattern MULTI_ROUTER_ROUTER \
  --recv recv \
  --msg-sizes 64 \
  --warmup 1 \
  --duration 1 \
  --results-tag codex_repro_router_router_ready_fix
```

Observed:

```text
Bad address (/home/hep7/project/kairos/zlink/core/src/sockets/fq.cpp:68)
SIGABRT: abort
...
zlink._Cfunc_zlink_recv(...)
...
main.runMultiRouterRouter.func2(...)
```

Control case:

```bash
./run_benchmarks_multi.sh \
  --pattern MULTI_DEALER_ROUTER \
  --recv recv \
  --msg-sizes 64 \
  --warmup 1 \
  --duration 1 \
  --clients 1 \
  --results-tag codex_repro_dealer_router_clients1
```

This completes successfully, which suggests the failure requires concurrent multi-client routed traffic rather than the basic single-client path.

## Expected

- Routed multi recv perf should complete for baseline-comparable client counts (`100` for routed patterns in this bindings perf surface).
- No core/native `Bad address` or abort should appear during normal concurrent routed traffic.

## Actual

- Concurrent multi routed perf aborts or returns native recv errors before comparable reports are produced.
- This blocks final comparable recv/callback verification for the active `go` bindings perf iteration.

## Impact

- `go` cannot be marked `completed` for the current bindings perf execution.
- Routed multi perf comparisons against the core recv baseline are currently blocked.

## Related Evidence

- successful single-client control report:
  `/home/hep7/project/kairos/zlink/bindings/go/perf/results/multi/report/perf_linux_recv_20260402_130450_codex_repro_dealer_router_clients1.txt`
- failing routed multi attempts were triggered from the commands above during the current session
- latest recv probe showing the active language is still under work:
  `/home/hep7/project/kairos/zlink/bindings/go/perf/results/multi/report/perf_linux_recv_20260402_125946_codex_probe_recv_refresh.txt`

## Conclusion

Review feedback:

This issue currently reads as if concurrent routed traffic in `core` has already
been proven broken. The evidence does not support that conclusion yet.

What is proven today is narrower: the failure is reproducible from the
`bindings/go/perf` surface, and that surface hits `Bad address` / abort paths
while running routed multi-client recv flows. That is sufficient to classify
the issue as a real bug blocking the Go bindings perf work.

What is not yet proven is that the same failure reproduces from a repository
regression under `core/tests/` without the Go binding layer. Until that exists,
the safest classification is: `bindings/go` library or Go-to-core integration
bug, with possible `core` involvement still under investigation.

Recommended interpretation for follow-up:

- Track this as the active Go bindings blocker.
- Do not state it as a confirmed generic `core` concurrent-clients bug yet.
- Reclassify it as a `core` bug only after the same failure is reproduced from
  a repository regression in `core/tests/`.
