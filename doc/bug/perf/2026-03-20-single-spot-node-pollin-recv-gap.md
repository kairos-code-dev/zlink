# single `SPOT node` recv model used different sub subjects for `POLLIN` and `subscribe()`

## Summary

`core/perf/single/src/perf_spot.cpp` 를 policy대로 `recv = poller POLLIN drain`
구조로 정리하던 중, `SPOT --recv recv` 가 active 0으로 실패했다.

원인은 perf workaround가 아니라 core public API subject mapping mismatch였다.

- `zlink_publish(node, ...)` 는 `spot_node_t::ensure_default_pub()` 를 사용한다.
- `zlink_subscribe(node, ...)` 는 `spot_node_access_t::ensure_internal_receiver()` 를 사용한다.
- 하지만 `zlink_poller_add(node, ..., ZLINK_POLLIN)` 은 node의 `default_sub` 를
  poller subject로 사용하고 있었다.

즉 `SPOT node` recv model에서 app이 `node` handle 하나로

1. `zlink_poller_add(node, ZLINK_POLLIN)`
2. `zlink_subscribe(node, ...)`

를 조합하면, poller와 recv가 서로 다른 internal sub object를 보고 있었다.

## Why this is a bug

public header는 `zlink_spot_node_new()` 를 "recv model" handle로 설명하고,
recv model에서 writable readiness 는 poller를 사용한다고 명시한다.

같은 node handle에서 `POLLIN` readiness 와 `subscribe()` 가 다른 내부 subject를
보면 recv model contract를 만족하지 못한다.

## Reproduction

단일 프로세스에서 두 `SPOT node` 를 만들고:

1. server bind
2. client connect peer
3. client `set_subscription("bench")`
4. client monitor로 delivery-ready 대기
5. client `zlink_poller_add(node, node, ZLINK_POLLIN)`
6. server `zlink_publish(node, "bench", ...)`
7. client `zlink_poller_wait(...)` 후 `zlink_subscribe(node, ...)`

기존 구현에서는 publish는 성공하지만 `POLLIN`/`subscribe()` 조합이 delivery를
안정적으로 만들지 못했다.

회귀 테스트는 `core/tests/unittest/unittest_service_mode_policy.cpp` 에 추가했다.

## Fix direction

`zlink_poller_add()` 의 `SPOT node + POLLIN` 경로가
`default_sub` 가 아니라 `internal_receiver->impl()` 을 사용하도록 맞춘다.

이렇게 해야 `zlink_subscribe(node, ...)` 와 poller readiness 가 같은 recv subject를
공유한다.
