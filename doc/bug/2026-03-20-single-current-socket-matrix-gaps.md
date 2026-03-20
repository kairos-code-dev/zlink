# single current socket matrix still has `recv`/`callback` gaps

## Summary

Related classification:

- [`doc/bug/perf/2026-03-20-socket-matrix-failures-classification.md`](/home/hep7/project/kairos/zlink/doc/bug/perf/2026-03-20-socket-matrix-failures-classification.md)

single perf 전체 smoke를 다시 돌린 결과, README support matrix와 달리 아래 패턴이 아직
안정적으로 완료되지 않는다.

- `recv`
  - `PUBSUB`: timeout
  - `DEALER_DEALER`: timeout
  - `DEALER_ROUTER`: timeout
  - `ROUTER_ROUTER`: no data
- `callback`
  - `ROUTER_ROUTER`: no data

반면 같은 batch에서 아래는 정상 완료됐다.

- `PAIR` recv/callback
- `GATEWAY` recv/callback
- `SPOT` recv/callback
- `PUBSUB` callback
- `DEALER_DEALER` callback
- `DEALER_ROUTER` callback

즉 single current matrix는 아직 full support 상태가 아니다.

## Reproduction

### 1. single recv batch

```bash
timeout 180s env PYTHONDONTWRITEBYTECODE=1 \
  ./core/perf/run_benchmarks.sh \
  --pattern PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,GATEWAY,SPOT \
  --recv recv \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --warmup 1 \
  --duration 1
```

Observed failures:

- `PUBSUB current tcp 64B: timeout`
- `DEALER_DEALER current tcp 64B: timeout`
- `DEALER_ROUTER current tcp 64B: timeout`
- `ROUTER_ROUTER current tcp 64B: no_data`

### 2. single callback batch

```bash
timeout 180s env PYTHONDONTWRITEBYTECODE=1 \
  ./core/perf/run_benchmarks.sh \
  --pattern PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,GATEWAY,SPOT \
  --recv callback \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --warmup 1 \
  --duration 1
```

Observed failure:

- `ROUTER_ROUTER current tcp 64B: no_data`

## Why this is a bug

`core/perf/README.md` 와 `README_KO.md` 는 single matrix를 아래처럼 기술한다.

- recv: `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `GATEWAY`, `SPOT`
- callback: `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `GATEWAY`, `SPOT`

현재 smoke는 그 표면과 다르다. 따라서 남은 문제는 README가 아니라 실제 구현/runner bug다.

## Expected result

- single current matrix에서 문서에 기재된 패턴은 모두 64B/tcp smoke를 완료해야 한다.
- `recv` 는 timeout 없이 active result line을 출력해야 한다.
- `callback` 는 `ROUTER_ROUTER` 포함해서 `no_data` 없이 종료해야 한다.

## Suspected fix areas

- [`core/perf/single/src/perf_pubsub.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_pubsub.cpp)
- [`core/perf/single/src/perf_dealer_dealer.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_dealer_dealer.cpp)
- [`core/perf/single/src/perf_dealer_router.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_dealer_router.cpp)
- [`core/perf/single/src/perf_router_router.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_router_router.cpp)
- [`core/perf/single/common/bench_common.hpp`](/home/hep7/project/kairos/zlink/core/perf/single/common/bench_common.hpp)

## Current repo decision

- 이 문제는 support matrix 축소나 timeout 증가로 닫지 않는다.
- single current pattern implementation gap으로 추적한다.

## 2026-03-20 Validation Update

현재 워크스페이스(`2026-03-20`, local HEAD `96df2208`)에서 아래 smoke를 다시 실행했다.

```bash
timeout 180s env PYTHONDONTWRITEBYTECODE=1 \
  ./core/perf/run_benchmarks.sh \
  --pattern PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,GATEWAY,SPOT \
  --recv recv \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --warmup 1 \
  --duration 1

timeout 180s env PYTHONDONTWRITEBYTECODE=1 \
  ./core/perf/run_benchmarks.sh \
  --pattern PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,GATEWAY,SPOT \
  --recv callback \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --warmup 1 \
  --duration 1
```

Observed now:

- `recv`: all seven patterns complete
- `callback`: all seven patterns complete

Conclusion:

- 이 문서에 기록된 single matrix 실패는 현재 워크스페이스에서는 재현되지 않았다.
- 이번 턴에서는 single matrix에 추가 `core/` 수정이 필요하지 않았다.
