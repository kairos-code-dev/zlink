# SPOT Routed 성능 개선 작업 현황

> 작성일: 2026-04-23
> 목적: `SPOT` routed data plane 성능 개선 작업을 다른 작업자가 이어받을 수 있게
> 현재 상태, 설계 방향, 예상 성능, 주요 이슈, 다음 작업 순서를 정리한다.

## 1. 현재 결론

- 현재 병목의 핵심은 `SPOT_REQREP` 자체가 아니라 `SPOT` remote routed data
  plane 이다.
- `request/reply` 의미를 걷어낸 `MULTI_SPOT_SENDSEND` 도
  `MULTI_DEALER_ROUTER` 대비 크게 느리거나 run 마다 흔들린다.
- 따라서 pending/completion 같은 request bookkeeping 이 주원인은 아니다.
- 문제는 `spot -> spot_node -> spot_node -> spot` 경로 자체에 있다.

## 2. 비교 기준

- suite: `bindings/c/perf` multi
- transport: `tcp`
- payload: `64B`
- 주요 비교 패턴:
  - `MULTI_DEALER_ROUTER`
  - `MULTI_SPOT_SENDSEND`
  - `MULTI_SPOT_REQREP`

SPOT multi topology 해석은 perf 정책에 고정했다.

- [PERF_POLICY.md](/home/hep7/project/kairos/zlink/doc/perf/PERF_POLICY.md)
- [PERF_MULTI_TEST_POLICY.md](/home/hep7/project/kairos/zlink/doc/perf/PERF_MULTI_TEST_POLICY.md)

고정한 계약:

- `clients` 는 `SpotNode` 수가 아니라 **logical spot 수**다.
- 기본 topology 는 **client process 당 SpotNode 1개 + spot N개**다.
- 예: `--clients 100` 은 `SpotNode 1개 + spot 100개`를 뜻한다.

## 3. 현재 수치

### 3.1 정상 비교 기준

- `MULTI_DEALER_ROUTER tcp 64B`: 대략 `430~455 Kops/s`

### 3.2 SPOT 계열 관측값

1. direct route 작업 전

- `MULTI_SPOT_SENDSEND tcp 64B`: 대략 `45~55 Kops/s`
- `MULTI_SPOT_REQREP tcp 64B`: 대략 `45~50 Kops/s`

2. direct route 실험 중 불안정 상태

- `clients=1` 에서 `1 ops/s`, `0.333 ops/s` 같은 비정상 수치가 반복됨
- `clients=100` 에서 `CLIENT_READY` 실패가 자주 발생함

3. ready window 보정 후 일부 회복

- `clients=1, duration=3s`: `80.667 ops/s`
  - [perf_c_multi_linux_20260423_090121.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_090121.txt)
- `clients=100, duration=2s`: `32.218 Kops/s`
  - [perf_c_multi_linux_20260423_090225.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_090225.txt)
- 같은 방향의 코드 상태에서도 다시 `CLIENT_READY` failure 로 흔들림
  - [perf_c_multi_linux_20260423_090329.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_090329.txt)

정리하면, 현재는 **성능 부족**과 **경로 불안정**이 동시에 있다.

## 4. 기대 구조와 실제 문제

기대 구조는 아래와 같다.

```text
spot
-> local node router
-> remote node router
-> target spot recv surface
```

이 그림만 보면 `router -> router`에 `target spot recv` 하나 더 붙은 정도라서
`430 Kops/s`에서 `30~50 Kops/s`로 떨어지면 안 된다.

하지만 현재 구현은 아직 이 단순 경로로 완전히 닫혀 있지 않다.

### 4.1 기존 구조 문제

- remote routed traffic 이 원래 `mesh_pub/xsub` 경로를 탔다.
- local target delivery 도 `queue + signal + dispatch bookkeeping` 이 끼어
  있었다.
- 일부는 줄였지만, remote path 는 아직 완전히 단순화되지 않았다.

### 4.2 현재 direct route 문제

- `node_router <-> node_router` direct route 시도를 넣고 있다.
- sender socket 생성 직후 첫 실데이터가 너무 빨리 나가면 drop 되거나 ready phase가
  흔들린다.
- `peer_route_tx` 는 node 단위 shared sender 라서 multi-client 에서 interleave,
  warmup, fallback 타이밍이 꼬일 수 있다.
- 따라서 steady-state throughput 이전에 **경로 안정성**을 먼저 확보해야 한다.

## 5. 설계 방향

목표 구조는 아래와 같다.

```text
client spot
-> client node router
-> server node router
-> target spot recv surface
```

핵심 원칙:

- publish/sub fanout plane 과 routed direct plane 을 분리한다.
- remote routed message 는 `mesh_pub/xsub` 를 타지 않는다.
- local target spot delivery 는 최종적으로 `spot recv surface` 로 직접 닫혀야
  한다.
- `SPOT_SENDSEND` 와 `SPOT_REQREP` 는 같은 remote routed transport 를 쓰고,
  차이는 request/reply control 의미만 남게 정리한다.

## 6. 현재까지 반영된 작업

- local `spotnode -> spot` 전달을 `pending deque + signal byte` 대신
  multipart direct recv surface 쪽으로 이동
  - [service_spot_request_reply_queue.cpp](/home/hep7/project/kairos/zlink/core/src/api/service_spot_request_reply_queue.cpp)
- `recv_combined_router_message()` 첫 frame 유실 버그 수정
  - [service_spot_request_reply_local_dispatch.cpp](/home/hep7/project/kairos/zlink/core/src/api/service_spot_request_reply_local_dispatch.cpp)
- `MULTI_SPOT_SENDSEND` 패턴 추가
  - [perf_multi_spot_sendsend_client.cpp](/home/hep7/project/kairos/zlink/bindings/c/perf/multi/src/perf_multi_spot_sendsend_client.cpp)
  - [perf_multi_spot_sendsend_server.cpp](/home/hep7/project/kairos/zlink/bindings/c/perf/multi/src/perf_multi_spot_sendsend_server.cpp)
- perf runner 의 control link gating 버그 수정
  - [run_comparison.py](/home/hep7/project/kairos/zlink/bindings/c/perf/run_comparison.py)
- direct route sender warmup / ready-after window 실험 추가
  - [spot_runtime.cpp](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_runtime.cpp)
- direct routed send 에 runtime-level send lock 추가
  - [service_spot_request_reply_api.cpp](/home/hep7/project/kairos/zlink/core/src/api/service_spot_request_reply_api.cpp)

## 7. 목표 수치

- 최소 목표: `100 Kops/s` 이상에서 run 마다 흔들리지 않을 것
- 중간 목표: `200 Kops/s` 이상
- 최종 목표: `300 Kops/s` 이상

근거:

- `MULTI_DEALER_ROUTER` 가 `430~455 Kops/s` 수준이다.
- `SPOT` 는 `target spot recv surface` 와 routed envelope parse 정도의 추가 비용은
  감수하더라도 `30~50 Kops/s` 수준으로 떨어질 구조는 아니다.
- 따라서 `300 Kops/s`는 공격적이지만 충분히 노릴 만한 목표다.

## 8. 남은 주요 이슈

### 8.1 direct route 안정화

- sender 생성 직후 direct send 를 언제 허용할지 아직 불안정하다.
- warmup fallback 을 유지하면 첫 메시지는 살지만 steady-state 전환이 흔들린다.
- ready window 를 늘리면 `clients=1`은 나아질 수 있지만 `clients=100` ready phase를
  깨뜨릴 수 있다.

### 8.2 shared sender 구조

- 현재 direct route sender 는 node 단위 공유 소켓이다.
- multi-client 에서 이 공유 sender 를 계속 쓸지, per-target 또는 per-worker 로
  나눌지 결정이 필요하다.
- 현재는 send lock 을 넣었지만, 그것만으로 충분한지는 아직 확인되지 않았다.

### 8.3 local recv surface 최종 단순화

- 지금도 일부 경계에서 legacy dispatch/queue 의미가 남아 있다.
- 최종적으로는 target spot recv surface가 실제 최종 수신면이 되도록 더 줄여야
  한다.

## 9. 다음 작업 순서

1. direct route ready phase 를 먼저 안정화한다.
   - `clients=1`, `clients=10`, `clients=100` 순으로 고정 재현
   - `CLIENT_READY` failure 가 없어질 때까지 sender warmup/ready 전환 정리

2. shared `peer_route_tx` 구조를 다시 검토한다.
   - node 단위 shared sender 유지
   - worker 단위 sender
   - target endpoint 단위 sender
   세 방향을 비교

3. `SPOT_SENDSEND` 를 먼저 올린다.
   - `SPOT_REQREP` 보다 `SPOT_SENDSEND` 가 먼저 올라야 한다.
   - `SPOT_SENDSEND` 가 오르지 않으면 문제는 req/rep 계층이 아니다.

4. `SPOT_SENDSEND` 가 안정화되면 그 다음 `SPOT_REQREP` 를 다시 잰다.

## 10. 감독 이력

이 섹션은 자동화 감독 루프가 실행한 작업과 perf 결과를 순서대로 기록한다.
각 이터레이션마다 작업 내용, 빌드/테스트 결과, 다음 판단을 남긴다.

| 이터레이션 | 날짜       | 작업 요약                                  | SENDSEND tcp 64B (clients=100) | 판단            |
|-----------|-----------|------------------------------------------|-------------------------------|-----------------|
| 0         | 2026-04-23 | ready window 보정 후 초기 상태                | 32.218 Kops/s (불안정)          | 계속: 이슈 8.1 착수 |

---

## 11. 주요 파일

- core direct route
  - [service_spot_request_reply_api.cpp](/home/hep7/project/kairos/zlink/core/src/api/service_spot_request_reply_api.cpp)
  - [spot_runtime.cpp](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_runtime.cpp)
  - [spot_data_plane_protocol.cpp](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_data_plane_protocol.cpp)
  - [spot_data_plane_runtime.cpp](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_data_plane_runtime.cpp)
  - [spot_data_plane_loop.cpp](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_data_plane_loop.cpp)

- local spot delivery
  - [service_spot_request_reply_queue.cpp](/home/hep7/project/kairos/zlink/core/src/api/service_spot_request_reply_queue.cpp)
  - [service_spot_request_reply_local_dispatch.cpp](/home/hep7/project/kairos/zlink/core/src/api/service_spot_request_reply_local_dispatch.cpp)

- perf harness
  - [perf_multi_spot_sendsend_client.cpp](/home/hep7/project/kairos/zlink/bindings/c/perf/multi/src/perf_multi_spot_sendsend_client.cpp)
  - [perf_multi_spot_sendsend_server.cpp](/home/hep7/project/kairos/zlink/bindings/c/perf/multi/src/perf_multi_spot_sendsend_server.cpp)
  - [run_comparison.py](/home/hep7/project/kairos/zlink/bindings/c/perf/run_comparison.py)
