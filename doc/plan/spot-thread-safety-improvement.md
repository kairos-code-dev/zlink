# SPOT Pub/Sub Thread-Safety Improvement Plan

[한국어](spot-thread-safety-improvement.ko.md)

This file is the English companion to
`doc/plan/spot-thread-safety-improvement.ko.md`.
The Korean document is the detailed source of truth for the phased plan.

## Scope

- Reduce contention in `spot_pub` thread-safe publish path.
- Decouple `spot_sub` queue synchronization from node-global lock coupling.
- Expand thread-safety guarantees in small, backward-compatible phases.

## Phases

1. Baseline and stress reproducibility
2. `spot_sub` queue lock split + concurrent `recv` guard
3. Control-plane serialization hardening (`subscribe/unsubscribe/handler/destroy`)
4. Optional async publish mode (`SYNC` default, `ASYNC` opt-in)
5. Docs/bindings alignment and rollout decision

## Implementation Status (2026-02-12)

- Phase 1 implemented:
  queue-local sync for `spot_sub`, concurrent `recv` guard (`EBUSY`), tests.
- Phase 2 partially implemented:
  stronger detached-sub checks and destroy/remove serialization hardening.
- Phase 3 initial implementation completed:
  optional async publish mode via `zlink_spot_node_setsockopt` with:
  - `ZLINK_SPOT_NODE_OPT_PUB_MODE` (`SYNC` default, `ASYNC` opt-in)
  - `ZLINK_SPOT_NODE_OPT_PUB_QUEUE_HWM`
  - `ZLINK_SPOT_NODE_OPT_PUB_QUEUE_FULL_POLICY` (`EAGAIN` default or drop)
- Added API docs and scenario tests for async mode and option validation.

## Compatibility

- Keep existing API signatures.
- Keep `SYNC` publish semantics as default.
- Preserve raw socket non-exposure policy.
