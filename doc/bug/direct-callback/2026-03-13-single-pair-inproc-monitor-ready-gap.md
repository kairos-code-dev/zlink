# Single PAIR inproc monitor-ready gap

## Summary

- 이 이슈는 `core` raw monitor contract 버그였다.
- `PAIR inproc` 연결 완료 시 bind/connect 양쪽에서 perf gate가 사용할
  `ZLINK_EVENT_CONNECTION_READY`가 안정적으로 올라오지 않았다.
- 원인은 `inproc` bind side가 async `bind` command를 자기 thread에서 처리하기
  전까지 raw ready event가 없었고, single perf는 그 동안 monitor event만
  기다리다가 benchmark body에 들어가지 못한 점이다.
- 현재는 `core` 수정과 `core/tests` 회귀, direct/wrapper perf 재검증까지 끝난
  상태다.

## Root Cause

- network transport는 engine thread가 socket monitor event를 직접 올린다.
- 하지만 `inproc`는 transport engine이 없고, bind side 연결 완료가
  `send_bind(...)` command 처리 시점에만 반영됐다.
- single perf의 `setup_connected_pair()`는 bind/connect 양쪽에서
  `max(connection_ready_count, accepted_count) >= 1`을 요구한다.
- `PAIR inproc`에서는 bind side socket thread가 아직 `bind` command를 처리하지
  않은 동안 ready event가 오지 않아 `wait_connect_ready_count()`가 실패했다.

## Fix

- `core/src/sockets/socket_base.cpp`
  - inproc 연결 경로에서 raw `CONNECTION_READY`를 직접 emit하는 helper 추가
  - connect side local attach 직후 ready emit
  - peer bind side에도 연결 완료 시점에 ready emit
- `core/src/core/ctx.cpp`
  - pending inproc 연결 해소 시 bind/connect 양쪽에 ready emit
- `core/src/core/pipe.hpp`
  - pipe별 `CONNECTION_READY` 중복 emit 방지 flag 추가
- `core/tests/integration/monitoring/test_monitor_perf_contract.cpp`
  - `perf_pair` gate와 동일한 방식의 `PAIR inproc` 회귀 테스트 추가

## Regression Coverage

- 새 회귀:
  - `test_pair_inproc_perf_like_monitor_ready_implies_bidirectional_delivery`
- 이 테스트는 아래 흐름을 그대로 검증한다.
  - `bind`
  - raw monitor open
  - `connect`
  - bind/connect 양쪽 `wait_perf_like_connect_ready()`
  - 첫 양방향 송수신 성공

## Verification

### Core regression

```bash
ctest --test-dir build-codex --output-on-failure \
  -R '^test_monitor_perf_contract$'
```

- 결과: PASS

### Direct benchmark

```bash
env PERF_DEBUG=1 \
  PERF_SINGLE_DURATION_SECONDS=2 \
  PERF_SINGLE_SNDTIMEO_MS=200 \
  PERF_SINGLE_RCVTIMEO_MS=200 \
  ./core/build/bin/perf_pair current inproc 1024
```

- 결과: `RESULT,...` line 정상 출력
- 예:

```text
RESULT,current,PAIR,inproc,1024,throughput,2083614.00
RESULT,current,PAIR,inproc,1024,rcv_pending_end,0.00
```

### Wrapper

```bash
./core/perf/run_benchmarks.sh \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink-direct-callback-rewrite/core/build \
  --pattern PAIR \
  --transports inproc \
  --msg-sizes 1024 \
  --runs 1 \
  --duration 2
```

- 결과: `status: complete`
- 결과 파일:
  - `/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/perf/results/single/report/perf_linux_20260313_230547.txt`

## Resolution

- `PAIR inproc`는 현재 raw monitor ready model을 만족한다.
- default single perf transport 조합에 남아 있어도 된다.
