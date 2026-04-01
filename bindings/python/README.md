# Python Binding

Policy-aligned Python binding for `zlink`.

This package must stay aligned with:

- `bindings/README.md`
- `core/include/zlink.h`
- `doc/perf/PERF_POLICY.md`
- `doc/perf/PERF_SINGLE_TEST_POLICY.md`
- `doc/perf/PERF_MULTI_TEST_POLICY.md`

The Python surface follows the binding policy instead of mirroring the raw C
API directly. The public contract is:

- multipart-only send and receive
- blocking APIs use direct names like `send`, `recv`, `publish`
- non-blocking APIs use `try*` names like `try_send`, `try_recv`,
  `try_publish`
- receive returns domain objects such as `Received`, `Subscribed`, and
  `SubscriptionEvent`
- raw public option bags like `setsockopt` and `getsockopt` are not exposed
- typed option families are exposed through properties and capability objects
- monitor sockets use canonical `recv()` and `try_recv()` entrypoints

## Surface Summary

Socket capabilities are split by type instead of one generic bag of unrelated
methods:

- `PairSocket`
- `DealerSocket`
- `RouterSocket`
- `StreamSocket`
- `PubSocket`
- `SubSocket`
- `XPubSocket`
- `XSubSocket`
- `SpotNode` / `Spot`
- `Registry` / `Discovery`

Common hot-path helpers are value-typed:

- `Message`
- `Received`
- `Subscribed`
- `SubscriptionEvent`
- `SendResult`

## Verification

Run tests from `bindings/python`:

```bash
tests/run_tests.sh
```

The suite covers:

- canonical public surface and legacy API removal
- blocking/non-blocking behavioral contract
- ownership and multipart receive semantics
- monitor and discovery/service flows
- perf runner smoke execution
- perf/bench fastpath helper contract

## Perf

The official Python perf surface lives under `perf/`.

- [perf/README.md](/home/hep7/project/kairos/zlink/bindings/python/perf/README.md)
- [perf/multi/README.md](/home/hep7/project/kairos/zlink/bindings/python/perf/multi/README.md)

Perf code is a verification surface, not a workaround layer for binding or core
bugs. Hot-path measurements should stay close to the canonical Python receive
and publish/send paths and must not hide extra copies, conversions, or helper
layers behind the benchmark wrapper.
