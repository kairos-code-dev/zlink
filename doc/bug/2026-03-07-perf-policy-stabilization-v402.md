# 2026-03-07 PERF Policy Stabilization and Bug Report

## Scope

- `doc/perf`
- `core/perf`
- `core/src/services/gateway`
- validation:
  - representative perf smoke
  - full single perf
  - full multi perf
  - `ctest --output-on-failure`

## Summary

이번 라운드에서 확인된 실제 문제는 두 가지였다.

1. core `gateway`가 첫 service pool 생성 시 refresh task를 즉시 깨우지 않아,
   discovery에 서비스가 이미 떠 있어도 첫 snapshot이 비어 있을 수 있었다.
2. `core/perf`의 multi `gateway` server loop가 backpressure threshold를 넘긴 뒤에도
   같은 iteration에서 계속 drain을 시도해 deferred queue overflow로 죽었다.

첫 번째는 core bug였고, 두 번째는 perf implementation bug였다. 둘 다 수정했고,
최종적으로 representative/full perf와 `ctest`가 모두 green이다.

## Confirmed Bugs

### 1. Core bug: first gateway pool could stay stale until a later refresh

- area:
  - `core/src/services/gateway/gateway.cpp`
- symptom:
  - `gateway_connection_count()` or early send path가 discovery에 이미 존재하는
    service를 즉시 보지 못함
  - 초기 perf smoke에서 `gateway connection ready 0/N`, `EFSM`, preflight 실패로 이어짐
- root cause:
  - 새 service pool을 처음 만들 때 `dirty=true`로만 표시하고 refresh task를
    즉시 wakeup하지 않았다.
  - not-ready/inflight endpoint가 남아 있을 때도 다음 tick 재평가가 유지돼야 하는데,
    이 경계가 약하면 초기 snapshot이 stale 상태로 남는다.
- fix:
  - pool 생성 직후 pending update에 넣고 refresh task를 깨우도록 수정:
    [gateway.cpp](/home/hep7/project/kairos/zlink/core/src/services/gateway/gateway.cpp#L287)
  - not-ready endpoint는 다음 refresh까지 dirty 상태를 유지:
    [gateway.cpp](/home/hep7/project/kairos/zlink/core/src/services/gateway/gateway.cpp#L432)
- regression coverage:
  - `test_gateway_refreshes_existing_service_on_first_connection_count`
    [test_gateway.cpp](/home/hep7/project/kairos/zlink/core/tests/discovery/test_gateway.cpp#L2052)

### 2. Perf bug: multi gateway server overflowed its deferred queue under backpressure

- area:
  - `core/perf/multi/src/perf_gateway_server.cpp`
- symptom:
  - `run_benchmarks_multi.sh --pattern GATEWAY ...`가 개별 size run은 통과하지만,
    여러 size/transport를 한 runner process에서 연속 실행하면 후반에
    `server_non_zero_exit`
  - direct stderr:
    - `gateway server: deferred queue overflow <N>`
- root cause:
  - server loop는 pending response가 이미 threshold에 도달한 뒤에도
    같은 `POLLIN` cycle 안에서 추가 요청을 계속 drain했다.
  - receiver `POLLIN`을 꺼도 이미 들어온 drain loop가 멈추지 않아
    deferred queue가 터졌다.
- fix:
  - pending storage를 고정 배열로 두고 hot loop allocation을 제거:
    [perf_gateway_server.cpp](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_gateway_server.cpp#L539)
  - `pending_backpressure_threshold`를 도입하고 threshold 이상이면
    receiver `POLLIN`을 끔:
    [perf_gateway_server.cpp](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_gateway_server.cpp#L611)
  - inner drain loop도 threshold 도달 즉시 break:
    [perf_gateway_server.cpp](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_gateway_server.cpp#L652)

## Policy Alignment Applied

정책에 맞춰 다음을 반영했다.

- single:
  - `blocking send 1회 + blocking recv + same-iteration nonblocking drain`
  - `SPOT` readiness는 기존 `poll/backoff` probe를 제거하고 blocking handshake로 단순화
- multi:
  - `recv = PollIn + nonblocking drain to EAGAIN`
  - `send = DONTWAIT 1회 + pending + PollOut on-demand`
  - `GATEWAY` preflight도 active loop와 같은 pending/PollOut contract로 재작성
- hot loop:
  - `yield/sleep/backoff` 기반 우회를 제거
  - `SPOT` recv batch cap 제거
  - legacy throughput helper 제거

주요 반영 파일:

- [perf_spot.cpp](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_spot.cpp)
- [perf_gateway.cpp](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_gateway.cpp)
- [perf_gateway_client.cpp](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_gateway_client.cpp)
- [perf_gateway_server.cpp](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_gateway_server.cpp)
- [perf_spot_client.cpp](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_spot_client.cpp)
- [perf_common.hpp](/home/hep7/project/kairos/zlink/core/perf/multi/common/perf_common.hpp)
- [run_comparison.py](/home/hep7/project/kairos/zlink/core/perf/run_comparison.py)

## Validation

### Representative perf

- single representative passed:
  - `./core/perf/run_benchmarks.sh --pattern PAIR,DEALER_ROUTER,ROUTER_ROUTER_POLL,GATEWAY,SPOT --transports tcp --msg-sizes 64 --runs 1 --duration 1 --reuse-build`
  - result: [perf_linux_20260307_162613.txt](/home/hep7/project/kairos/zlink/core/perf/results/single/report/perf_linux_20260307_162613.txt)
- multi representative passed:
  - `./core/perf/run_benchmarks_multi.sh --pattern DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,PUBSUB,GATEWAY,SPOT,STREAM,STREAM_CALLBACK,STREAM_LEN32BE --transports tcp --msg-sizes 64 --runs 1 --clients 2 --warmup 0 --duration 1 --reuse-build`
  - result: [perf_linux_20260307_162613.txt](/home/hep7/project/kairos/zlink/core/perf/results/multi/report/perf_linux_20260307_162613.txt)

### Full perf

- full single passed:
  - `./core/perf/run_benchmarks.sh --pattern ALL --runs 1 --reuse-build`
  - result: [perf_linux_20260307_162802.txt](/home/hep7/project/kairos/zlink/core/perf/results/single/report/perf_linux_20260307_162802.txt)
  - status: `complete`
- full multi passed:
  - `./core/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --reuse-build`
  - result: [perf_linux_20260307_174549.txt](/home/hep7/project/kairos/zlink/core/perf/results/multi/report/perf_linux_20260307_174549.txt)
  - success: `192`
  - fail: `0`
  - expected result lines: `960`
  - actual result lines: `960`
  - status: `complete`

### Targeted repros that now pass

- `GATEWAY` multi single transport/size progression:
  - `./core/perf/run_benchmarks_multi.sh --pattern GATEWAY --transports tcp --msg-sizes 64,256,1024,65536,131072,262144 --runs 1 --clients 100 --warmup 2 --duration 5 --reuse-build`
  - result: [perf_linux_20260307_172642.txt](/home/hep7/project/kairos/zlink/core/perf/results/multi/report/perf_linux_20260307_172642.txt)
- `STREAM*` wss tail repro:
  - `./core/perf/run_benchmarks_multi.sh --pattern STREAM --transports wss --msg-sizes 64,256,1024,65536 --runs 1 --reuse-build`
  - result: [perf_linux_20260307_174242.txt](/home/hep7/project/kairos/zlink/core/perf/results/multi/report/perf_linux_20260307_174242.txt)

### Core tests

- `ctest --output-on-failure` passed on rerun:
  - 85/85 passed, 4 skipped
- `test_gateway` had one transient timeout during an earlier `ctest` run but was not reproducible:
  - direct binary rerun passed in about 7 seconds
  - `ctest -R '^test_gateway$|^test_gateway_handover$'` passed
  - full `ctest` rerun passed

## Final Assessment

- confirmed core bug: yes
  - `gateway` first-refresh wakeup / dirty refresh continuity
- confirmed perf bug: yes
  - `multi gateway server` deferred queue overflow under backpressure
- workaround used: no
  - timeout 확대, retry budget, fallback success 처리로 덮지 않았다.
- final status:
  - representative perf green
  - full single perf green
  - full multi perf green
  - full `ctest` green
