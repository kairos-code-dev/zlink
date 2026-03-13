# Multi SPOT first-delivery gap after both delivery-ready events

## Status

Closed in `core`.

Publisher-side first-delivery gating is now:

- subscriber: `ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED(value == 1)`
- publisher: `ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED(value >= N)`

Using `ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED` alone as the first-publish gate
is no longer the recommended contract.

## Root cause

The gap was caused by multiple core issues on the SPOT monitor/data-plane path.

1. `spot_data_plane` treated `socket_poller_t::wait()` timeout/no-event
   (`EAGAIN`) as a fatal error.
   - this could stop the ready-probe loop before all subscribers observed the
     first delivery-safe barrier
2. SPOT service-monitor event serialization used
   `moodycamel::ConcurrentQueue::size_approx()` as a correctness condition.
   - this could leave `ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED` queued but
     never drained
3. Publisher-side first-delivery signaling needed to follow the ready-ack
   barrier directly.
   - once the publisher has received ready-acks, that count is the
     first-delivery-safe aggregate gate

## Core fix

The fix in `core` is:

- ignore `EAGAIN` / `EINTR` from `socket_poller_t::wait()` in
  `core/src/services/spot/spot_data_plane.cpp`
- keep SPOT ready-probe windows alive across duplicate/late subscribe churn
- emit `ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED` from publisher
  ready-ack-count changes
- drain SPOT monitor events with an exact pending counter instead of
  `size_approx()`

## Regression coverage

The perf symptom is now covered by `core/tests` regressions instead of relying
on perf binaries.

- `test_monitor_service_contract_spot_single`
  - `SUB_DELIVERY_READY_CHANGED(1)` plus
    `PUB_FIRST_DELIVERY_READY_CHANGED(1)` implies first publish delivery
- `test_monitor_service_contract_spot_multi`
  - with `client_count = 16`,
    `PUB_FIRST_DELIVERY_READY_CHANGED(value >= 16)` implies first publish
    delivery to every subscriber

## Verification

Verified in `/home/hep7/project/kairos/zlink-direct-callback-rewrite/build-codex`
on March 13, 2026 with:

```bash
ctest --test-dir build-codex --output-on-failure -R \
  '^(test_monitor_service_contract_spot_single|test_monitor_service_contract_spot_multi)$'
```

Observed:

- `test_monitor_service_contract_spot_single` PASS
- `test_monitor_service_contract_spot_multi` PASS

The integration lane still has one unrelated failure:

- `test_service_discovery_heartbeat_timeout` timed out in the current tree

That failure is outside the SPOT first-delivery change.
