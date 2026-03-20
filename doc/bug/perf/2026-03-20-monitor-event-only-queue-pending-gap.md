# 2026-03-20 monitor event-only queue pending gap

## Summary

`core/perf` policy now requires `monitor callback` event consumption only and
forbids `monitor_snapshot` polling. However, the current public monitor event
surface does not expose queue depth deltas or queue depth snapshots as events.
That means perf cannot obtain `snd_pending_max`, `rcv_pending_max`,
`rcv_pending_end` from monitor events alone.

## Impact

- perf start gate can use delivery-ready monitor events and is unaffected
- informational queue pending metrics cannot be sourced from monitor events
- using `zlink_monitor_snapshot()` in perf would violate the perf policy

## Current perf handling

- single perf uses benchmark-side message flow accounting for informational
  queue metrics
- multi perf keeps event-driven/public-state counters where available
  (`MULTI_GATEWAY`, `MULTI_SPOT`) and otherwise emits default `0`
- perf no longer uses `monitor_snapshot` as a workaround

## Requested core follow-up

Expose queue pending telemetry on the public monitor event path so perf can
report informational queue metrics without snapshot polling.
