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
- non-blocking behavior is expressed through `SendFlags` and `RecvFlags`
- `Context` exposes typed `ContextOptions` instead of raw `set/get`
- `Message` exposes zero-copy `data` plus diagnostic `get_property` and
  `ref_count`
- receive and subscribe return domain objects such as `Received`,
  `TopicMessage`, `RoutingId`, and `SubscriptionEvent`
- request/reply surfaces use `request`, `reply`, `request_to_spot`,
  `recv_routed`, and `reply_to_router`
- raw public option bags like `setsockopt` and `getsockopt` are not exposed
- typed option families are exposed through properties and capability objects
- monitor sockets use canonical `monitor_open()`, `recv()`, and `snapshot()`
- monitor masks use `MonitorEventMask`; decoded monitor payload uses
  `MonitorEvent`
- resource-owning types support sync and async context manager cleanup
- `*_READY_CHANGED` monitor events do not expose aggregate ready counts
- monitor snapshots are state/queue inspection surfaces, not ready-count gates
- callback registration uses canonical names `on_packet`, `on_send_ready`,
  and `on_dispatch_event`; topic subscription uses
  `subscribe_into()` / `receive_subscription_event_into()` instead of a direct
  `on_subscribe` callback. SPOT routed receive and Actor lifecycle are drained
  explicitly with `recv_routed_into()` / `recv_actor_lifecycle()` after a
  dispatch readable event.
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

- `SubSocket` exposes `subscribe`, `set_subscription`, and
  `unset_subscription`, but not direct `recv` or a direct subscription
  callback
- `XPubSocket` is the only Python socket surface that exposes
  `receive_subscription_event`
- `StreamSocket` keeps routed send/receive but does not expose generic
  `connect` / `disconnect`
- `SpotNode` owns SPOT topology and channel call attachments. It exposes
  `attach_channel_dealer`, `attach_channel_dealer_manual`, and
  `attach_pub_ingress` on top of discovery and topology management.
- `Spot` is a service-aware pub/sub and routed facade on top of `SpotNode`;
  it exposes `publish(channel_name, topic, ...)`, `send_channel`,
  `request_channel`, `subscribe_into`, `receive_subscription_event_into`,
  `set_subscription`, `unset_subscription`, `on_send_ready`,
  `reply_to_spot`, `reply_to_router`, `recv_routed_into`,
  `recv_actor_lifecycle`, and `on_dispatch_event`, but not direct `recv` / `send`
- `attach_discovery()` is only available on the discovery-aware socket subset,
  and after `attach_discovery` the native lifecycle contract blocks manual
  `connect`, `disconnect`, `unbind`, and `close`

Common hot-path helpers are value-typed:

- `Message`
- `Received` with `send(...)` for normal routed send-back over the original
  receive context and `reply(...)` for request-reply messages
- `TopicMessage`
- `RoutingId`
- `SubscriptionEvent`
- `SendFlags`
- `RecvFlags`
- `SubmitResult`
- `RequestResult`
- `RecvResult`
- `HandlerResult`
- `CloseResult`
- `BindResult`
- `ConnectResult`
- `ConfigResult`
- `Timer`
- `Stopwatch`
- `Thread`
- `AtomicCounter`

Service and topology helpers are also surfaced as domain objects:

- `ServiceEvent`
- `MemberPeerEntry`
- `RegistryStatus`
- `RegistryTopologyEntry`
- `RegistryServiceSummaryEntry`
- `RegistryTopologyFilter`
- `RegistryServiceSummaryFilter`

`TopicMessage` and `SubscriptionEvent` carry `channel_name` for service-aware
SPOT flows. Raw `SUB` / `XSUB` results leave that field empty.

## Boundary Rules

The Python binding fail-fast validates values before the native call when the
policy requires it:

- endpoint, topic, and subscription strings/bytes reject embedded NUL
- fixed-size `channel_name` and endpoint inputs fail fast above 255 bytes
- `RoutingId` enforces the native 255-byte maximum
- typed integer options fail on signed/unsigned overflow instead of truncating
- send/receive convenience does not change the multipart-only contract
- blocking send/publish inside receive callbacks raises an explicit error
  instead of silently degrading to non-blocking behavior; use
  `SendFlags.DONT_WAIT` or `RecvFlags.DONT_WAIT` for explicit
  non-blocking behavior

## Typed Options

The canonical Python option facades are:

- `ContextOptions`
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

Readiness gates in Python perf and samples must use low-cost event counting
rather than monitor payload counts or monitor snapshot ready counts.
- raw sockets: `CONNECTION_READY` event counting
- SPOT: explicit benchmark barrier protocol; no separate service-event gate
