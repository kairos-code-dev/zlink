# Node perf public API gaps against PERF_POLICY

## Summary

`bindings/node/perf` still has several policy gaps that should not be hidden by
perf-only workarounds. This note also records the SPOT dispatch-drain bug fixed
on 2026-05-14, because it was the first blocker found while applying the policy.

`doc/perf/PERF_POLICY.md` requires binding perf to use only public binding APIs,
and says public API behavior problems should be reported rather than bypassed in
perf code. The same policy also requires SPOT dispatch-event activation and a
public poller path for `MULTI_SPOT_REQREP` requester reply completion.

## Gaps

### 1. Fixed: SPOT dispatch-event drain returned busy after callback delivery

Affected paths:

- `bindings/node/perf/single/perf_spot.ts`
- `bindings/node/perf/multi/perf_multi_spot_client.ts`
- `bindings/node/perf/multi/perf_multi_spot_reqrep_server.ts`
- `bindings/node/perf/multi/perf_multi_spot_sendsend_server.ts`

Observed while attempting to move recv drain behind `Spot.onDispatchEvent(...)`:

- Calling `subscribe(...)` or `recvRouted(...)` from the dispatch callback, or
  shortly after it on the next event-loop tick, can report
  `Device or resource busy`.
- During shutdown, delayed drain attempts can also surface `Bad address` after
  the Spot has started closing.

The root problem was that the core SPOT receive path treated a registered
dispatch handler as an exclusive callback context. Node delivers the dispatch
event through an async callback, so the later public `subscribe(...)` or
`recvRouted(...)` call happened outside the core callback scope and was rejected
as busy.

Fix direction:

1. Allow dispatch-owned receive queues to be drained through the public SPOT
   receive calls after async callback delivery.
2. Keep direct request-handler receive ownership exclusive, because that path
   still represents a synchronous callback contract.
3. Add a focused Node regression test for `onDispatchEvent` followed by public
   `subscribe(...)`.
4. Remove perf-only busy-as-no-data guards so the same regression is not hidden
   again.

### 2. `MULTI_SPOT_REQREP` requester reply completion has no public poller path

Affected path:

- `bindings/node/perf/multi/perf_multi_spot_reqrep_client.ts`

The active path currently uses:

```text
spot.requestToSpot(...).submit(callback)
```

`PERF_POLICY.md` requires requester reply completion to proceed through a public
poller path. The Node public API currently exposes `requestToSpot(...).submit`
and `submitAsync()`, but no public poller-visible request completion subject.

Expected direction:

1. Decide the public Node contract for poller-driven SPOT request completion.
2. Add binding tests for that contract.
3. Refactor `MULTI_SPOT_REQREP` client to use that public poller path.

### 3. `MULTI_PUBSUB` cannot safely use only `poller.waitMany(..., -1)`

Affected path:

- `bindings/node/perf/multi/perf_multi_pubsub_client.ts`

An experiment changed the client recv loop to wait indefinitely and exit on the
server cooldown frame. Smoke testing then timed out because the client can miss
the cooldown wake and has no separate stop wake in the data path.

Expected direction:

1. Align the multi PUBSUB stop/phase-end contract with the C perf model.
2. Ensure the client has a reliable wire-level wake after active duration.
3. Only then replace the duration-based `waitMany(..., timeoutMs)` with an
   indefinite poller wait.

### 4. Node `MULTI_STREAM` client still uses raw transport code

Affected path:

- `bindings/node/perf/multi/perf_multi_stream_client.ts`

The server uses public `StreamSocket.onPacket(...)`, but the client builds raw
TCP/TLS/WS transports and its own frame reader. Policy says binding perf should
measure the binding public API data path, while the STREAM exception is the
public packet-handler surface, not arbitrary raw transport code.

Expected direction:

1. Decide whether Node STREAM perf should use `StreamSocket.recv(...)` or
   `StreamSocket.onPacket(...)` on the client side.
2. If the public API cannot connect STREAM clients directly by design, document
   the exact supported STREAM perf comparison surface.
3. Refactor the client after the contract is clear.
