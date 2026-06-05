# multi current `recv` matrix still fails on non-STREAM patterns

## Summary

Related classification:

- [`doc/bug/perf/2026-03-20-socket-matrix-failures-classification.md`](/home/hep7/project/kairos/zlink/doc/bug/perf/2026-03-20-socket-matrix-failures-classification.md)

multi recv batch smoke를 다시 돌린 결과, README support matrix와 달리 아래 패턴이 아직
완료되지 않는다.

- `DEALER_DEALER`: server non-zero exit
- `DEALER_ROUTER`: non-zero exit
- `ROUTER_ROUTER`: non-zero exit
- `PUBSUB`: non-zero exit at `CLIENT_READY,64`
- `STREAM`: pass

즉 multi current recv matrix는 아직 사실상 `STREAM`만 안정적으로 통과하고 있다.

## Reproduction

```bash
timeout 180s env PYTHONDONTWRITEBYTECODE=1 \
  ./core/perf/run_benchmarks_multi.sh \
  --pattern DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,PUBSUB,STREAM \
  --recv recv \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --warmup 1 \
  --duration 1 \
  --clients 2
```

Observed failures:

- `DEALER_DEALER current tcp 64B: ... server_non_zero_exit_1 ...`
- `DEALER_ROUTER current tcp 64B: missing_*_non_zero_exit_1`
- `ROUTER_ROUTER current tcp 64B: missing_*_non_zero_exit_1`
- `PUBSUB current tcp 64B: non_zero_exit_1_CLIENT_READY,64`

Observed success:

- `STREAM current tcp 64B` complete

## Why this is a bug

현재 README는 multi recv support를 아래처럼 기술한다.

- `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `PUBSUB`, `STREAM`

하지만 실제 smoke는 `STREAM`만 통과한다. 즉 policy/README와 구현이 아직 맞지 않는다.

## Expected result

- README에 적힌 multi recv 패턴이 모두 64B/tcp smoke를 통과해야 한다.
- `CLIENT_READY` 이후 phase/window가 정상 진행돼 throughput line을 출력해야 한다.
- pattern별 server/client가 non-zero exit 없이 종료해야 한다.

## Suspected fix areas

- [`core/perf/multi/src/perf_multi_dealer_dealer_server.cpp`](../../bindings/c/perf/multi/src/perf_multi_dealer_dealer_server.cpp)
- [`core/perf/multi/src/perf_multi_dealer_router_server.cpp`](../../bindings/c/perf/multi/src/perf_multi_dealer_router_server.cpp)
- [`core/perf/multi/src/perf_multi_router_router_server.cpp`](../../bindings/c/perf/multi/src/perf_multi_router_router_server.cpp)
- [`core/perf/multi/src/perf_multi_pubsub_server.cpp`](../../bindings/c/perf/multi/src/perf_multi_pubsub_server.cpp)
- [`core/perf/multi/common/perf_common.hpp`](../../bindings/c/perf/multi/common/perf_common.hpp)

## Current repo decision

- 이 문제는 README 축소나 mode별 예외 처리로 닫지 않는다.
- multi recv implementation gap으로 추적한다.

## 2026-03-20 Fix Update

이번 턴에서 `core` bug 하나를 수정했다.

- [`core/src/api/zlink.cpp`](../../core/src/api/core/zlink.cpp)
  - `zlink_recv(..., source_rid_out, ...)` 의 ROUTER 경로가 callback payload shape와 달리 routing envelope를 `parts[]`에 그대로 남기고 있었다.
  - 같은 경로에서 DEALER direct recv도 peer routing id를 `source_rid_out`로 흘려 public contract를 깨고 있었다.

추가한 회귀 테스트:

- [`core/tests/integration/test_multi_socket_contract_regressions.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_multi_socket_contract_regressions.cpp)
  - `test_router_recv_with_source_rid_strips_routing_envelope_from_dealer`
  - `test_dealer_recv_with_source_rid_hides_peer_routing_id`

Verification:

```bash
ctest --test-dir core/build --output-on-failure \
  -R '^test_multi_socket_contract_regressions$' -j1
```

Observed:

- pass

Perf re-run on `2026-03-20`:

```bash
timeout 180s env PYTHONDONTWRITEBYTECODE=1 \
  ./core/perf/run_benchmarks_multi.sh \
  --pattern DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,PUBSUB,STREAM \
  --recv recv \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --warmup 1 \
  --duration 1 \
  --clients 2
```

Observed now:

- `DEALER_DEALER`: pass
- `DEALER_ROUTER`: pass
- `ROUTER_ROUTER`: pass
- `STREAM`: pass
- `PUBSUB`: still fails at `CLIENT_READY,64`

Conclusion:

- `DEALER_ROUTER` / `ROUTER_ROUTER` failure는 `core` recv contract bug였고 이번 수정으로 닫혔다.
- 남은 `PUBSUB` 실패는 direct core callback/warmup regression이 아니라 perf path 이슈다.
- 관련 direct core verification은 [`core/tests/integration/test_multi_socket_contract_regressions.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_multi_socket_contract_regressions.cpp) 의 `test_pubsub_callback_remains_active_across_warmup_and_active_phases` 로 추가했다.
