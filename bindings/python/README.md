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

- multipart-only send and subscribe/receive
- the capability matrix from `bindings/README.md` is enforced by concrete
  socket types instead of one generic bag
- blocking APIs use direct names like `send`, `recv`, `publish`, `subscribe`
- non-blocking APIs use `try*` names like `try_send`, `try_recv`,
  `try_publish`, `try_subscribe`
- receive and subscribe return domain objects such as `Received`,
  `Subscribed`, `TopicMessage`, `RoutingId`, and `SubscriptionEvent`
- raw public option bags like `setsockopt` and `getsockopt` are not exposed
- typed option families are exposed through properties and capability objects
- monitor sockets use canonical `recv()` and `try_recv()` entrypoints
- service monitors use `recv()`, `try_recv()`, and `on_event()`
- `*_READY_CHANGED` monitor events do not expose aggregate ready counts
- monitor snapshots are state/queue inspection surfaces, not ready-count gates
- callback registration uses canonical names `on_receive`, `on_subscribe`,
  and `on_send_ready`
- callback removal by passing `None` is not part of the public contract;
  callback lifecycle ends with socket close

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
- `Registry` / `Discovery` / `RegistryQueryClient`

Examples of policy-enforced capability boundaries:

- `SubSocket` exposes `subscribe`, `try_subscribe`, `set_subscription`,
  `unset_subscription`, and `on_subscribe`, but not direct `recv`
- `XPubSocket` is the only Python socket surface that exposes
  `receive_subscription_event` / `try_receive_subscription_event`
- `StreamSocket` keeps routed send/receive but does not expose generic
  `connect` / `disconnect`
- `Spot` is a pub/sub service facade on top of `SpotNode`; it exposes
  `publish`, `try_publish`, `subscribe`, `try_subscribe`, `set_subscription`,
  `unset_subscription`, `on_subscribe`, and `on_send_ready`, but not
  `recv` / `try_recv` / `send` / `try_send`
- `attach_discovery()` is only available on the discovery-aware socket subset,
  and after `attach_discovery` the native lifecycle contract blocks manual
  `connect`, `disconnect`, `unbind`, and `close`

Common hot-path helpers are value-typed:

- `Message`
- `Received`
- `TopicMessage`
- `Subscribed`
- `RoutingId`
- `SubscriptionEvent`
- `SendResult`

Service and topology helpers are also surfaced as domain objects:

- `ServiceEvent`
- `MemberPeerEntry`
- `RegistryStatus`
- `RegistryTopologyEntry`
- `RegistryServiceSummaryEntry`
- `RegistryTopologyFilter`
- `RegistryServiceSummaryFilter`

## Boundary Rules

The Python binding fail-fast validates values before the native call when the
policy requires it:

- endpoint, topic, and subscription strings/bytes reject embedded NUL
- fixed-size `service_name` and endpoint inputs fail fast above 255 bytes
- `RoutingId` enforces the native 255-byte maximum
- typed integer options fail on signed/unsigned overflow instead of truncating
- send/receive convenience does not change the multipart-only contract

## Typed Options

The canonical Python option facades are:

- `CommonSocketOptions`
- `RouterSocketOptions`
- `DealerSocketOptions`
- `StreamSocketOptions`
- `PubSocketOptions`
- `SubSocketOptions`

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

## Samples

Run the canonical sample suite from `bindings/python`:

```bash
samples/run_samples.sh
```

## Perf

The official Python perf surface lives under `perf/`.

- [perf/README.md](/home/hep7/project/kairos/zlink/bindings/python/perf/README.md)
- [perf/multi/README.md](/home/hep7/project/kairos/zlink/bindings/python/perf/multi/README.md)

Perf code is a verification surface, not a workaround layer for binding or core
bugs. Hot-path measurements should stay close to the canonical Python receive
and publish/send paths and must not hide extra copies, conversions, or helper
layers behind the benchmark wrapper.

Readiness gates in Python perf and samples must use event counting rather than
monitor payload counts or monitor snapshot ready counts.
