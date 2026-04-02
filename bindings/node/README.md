# Node Bindings

Aligned Node bindings for `libzlink`.

## Canonical Raw API

- `new PairSocket(ctx)`, `new DealerSocket(ctx)`, `new RouterSocket(ctx)`,
  `new StreamSocket(ctx)`
- `new PubSocket(ctx)`, `new XPubSocket(ctx)`
- `new SubSocket(ctx)`, `new XSubSocket(ctx)`
- connection lifecycle: `bind(endpoint)`, `unbind(endpoint)`
- connectable sockets: `connect(endpoint)`, `disconnect(endpoint)`
- discovery attachment: `attachDiscovery(discovery)` on `DealerSocket`,
  `RouterSocket`, `PubSocket`, `SubSocket`
- publisher sockets: `publish(topic, message|parts)`,
  `tryPublish(topic, message|parts)`, `onSendReady(handler)`
- message sockets: `send(message|parts)`, `trySend(message|parts)`,
  `recv()`, `tryRecv()`, `onReceive(handler)`, `onSendReady(handler)`
- routed sockets: `send(routingId, message|parts)`,
  `trySend(routingId, message|parts)`, `recv()`, `tryRecv()`,
  `onReceive(handler)`, `onSendReady(handler)`
- subscriber sockets: `setSubscription(topicOrPattern)`,
  `unsetSubscription(topicOrPattern)`, `subscribe()`, `trySubscribe()`,
  `onSubscribe(handler)`
- `XPubSocket`: `receiveSubscriptionEvent()`,
  `tryReceiveSubscriptionEvent()`
- `StreamSocket`: `setRoutingId()`, `getRoutingId()`
- canonical option facades:
  - `CommonSocketOptions`
  - `DealerSocketOptions`
  - `RouterSocketOptions`
  - `StreamSocketOptions`
  - `PubSocketOptions`
  - `SubSocketOptions`
- socket option access: `socket.options.*`
- monitors: `recv()`, `tryRecv()`

`Message.copyOf()` copies payload ownership into the message.
`Message.wrap()` keeps the caller-owned buffer as the payload source.
Canonical raw sockets intentionally hide opposite-direction methods, so
`PubSocket` does not expose `send()` or `recv()`, `SubSocket` does not expose
`send()`, and `StreamSocket` does not expose `connect()` or active stream
helpers on the canonical path.

Canonical receive results are domain objects:

- `Received`: `{ routingId: Buffer | null, parts: Message[] }`
- `Subscribed`: `{ routingId: Buffer | null, topic: string, parts: Message[] }`
- `SubscriptionEvent`:
  `{ routingId: Buffer | null, topic: string, subscribed: boolean }`
- `SendResult`: `Sent`, `Backpressured`, `NotReady`

- generic `Socket` / `BaseSocket` are not exported from the aligned public API
- legacy flags-based send/recv, raw stream attach/detach helpers, and raw
  socket option bags are not part of the public package surface

Not part of the canonical or sample contract:
length-prefixed stream framing such as `len32be`.

Not yet part of the canonical raw surface:
raw socket TLS convenience helpers, raw publish(topic, payload).

## Service Surface

- `new Discovery(ctx, serviceType, serviceName)`
- `new Registry(ctx)` + `registry.bind(pubEndpoint, routerEndpoint)`
- `new RegistryQueryClient(ctx)`
- `new SpotNode(ctx)`
- `new Spot(node)`

`Spot` follows the same multipart/domain-return direction:
`publish()` / `tryPublish()`, `setSubscription()` / `unsetSubscription()`,
`subscribe()` / `trySubscribe()`, `onSubscribe()`, `onSendReady()`,
`monitorOpen()`.

`Discovery` uses `connectRegistry()`, `setValue()` / `getValue()`,
`setMetadata()` / `getMetadata()`, `memberPeers()`,
`memberPeerMetadata()`, `monitorOpen()`.

`SpotNode` uses `bind()`, `connectPeer()` / `disconnectPeer()`,
`attachDiscovery()`, `statusSnapshot()`, `peersSnapshot()`,
`peersQuery()`, `subjectsSnapshot(filter?)`, `monitorOpen()`.

`Registry` uses `bind()`, `setId()`, `addPeer()`, `setHeartbeat()`,
`setBroadcastInterval()`, `statusSnapshot()`,
`serviceSummarySnapshot(filter?)`, `memberPeers()`,
`memberPeerMetadata()`, `topologySnapshot()`, `topologyQuery(filter?)`.

`RegistryQueryClient` uses `connect()` and `snapshot(filter?)`.

`ServiceMonitor` uses `recv()`, `tryRecv()`, `onEvent()`, `snapshot()`,
`close()`. Service monitor events are returned as typed `ServiceEvent`
objects.

`*_READY_CHANGED` monitor events are readiness edge/state notifications.
Node bindings must not interpret `event.value` as an aggregate ready count, and
`snapshot()` must not be used as a ready-count gate.

`Receiver` is removed from the aligned public API.
`Discovery` requires a non-empty `serviceName`.
`Registry.bind()` maps directly to the native bind lifecycle and replaces legacy
`setEndpoints()` / `start()`.
`Registry.setSockOpt()` and `SpotNode.setDiscovery()` are not part of the
aligned canonical surface. `Spot` also does not expose raw `setSockOpt()`;
raw socket options are exposed through typed `socket.options` facades instead
of raw option bags or per-socket setter aliases. `Spot` keeps service-level
typed setters such as `setLinger()` and `setNoDrop()`.
After `attachDiscovery()`, manual socket/node connect-disconnect entry points
are blocked by the native lifecycle contract.

## Verification

```bash
cd bindings/node && npm run build
cd bindings/node && npm run rebuild-native
cd bindings/node && npm test
cd bindings/node && npm run samples
cd bindings/node && npm run perf:single -- --recv callback --pattern PAIR --warmup 0.2 --duration 0.5
cd bindings/node && npm run perf:multi -- --recv recv --pattern STREAM --warmup 0.2 --duration 0.5
```

## Perf Status

- single perf is implemented for `PAIR`, `PUBSUB`, `DEALER_DEALER`,
  `DEALER_ROUTER`, `ROUTER_ROUTER`, `SPOT`
- single perf supports `--recv callback` only
- multi perf is implemented for `MULTI_DEALER_DEALER`, `MULTI_PUBSUB`,
  `STREAM`
- multi perf supports:
  - `MULTI_DEALER_DEALER`: `--recv recv`
  - `MULTI_PUBSUB`: `--recv recv`
  - `STREAM`: `--recv recv|callback`
- perf structure and review criteria are defined by
  [`bindings/README.md`](/home/hep7/project/kairos/zlink/bindings/README.md)
  and the shared policy docs under
  [`doc/perf/`](/home/hep7/project/kairos/zlink/doc/perf)
- readiness gates in binding perf must use low-cost event counting, not
  aggregate ready counts from monitor payloads or snapshots
- raw sockets: `CONNECTION_READY_CHANGED` + fixed 1-second settle
- SPOT: `PEER_UP` + fixed 1-second settle
