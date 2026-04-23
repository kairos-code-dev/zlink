# SPOT Routed 성능 개선 작업 현황

> 작성일: 2026-04-23
> 목적: `SPOT` routed data plane 성능 개선 작업을 다른 작업자가 이어받을 수 있게
> 현재 상태, 설계 방향, 예상 성능, 주요 이슈, 다음 작업 순서를 정리한다.

## 0. 작업 운영 규칙

이 문서는 단순 상태 메모가 아니라, `SPOT_SENDSEND` / `SPOT_REQREP`를
`300 Kops/s`까지 끌어올리는 실행 규칙도 함께 기록한다.

### 0.1 종료 조건

- 종료 조건은 아래 셋 중 하나다.
  - `MULTI_SPOT_SENDSEND tcp 64B clients=100 duration=2s`가 `300 Kops/s` 이상
    `3회 연속` 통과
  - `MULTI_SPOT_REQREP tcp 64B clients=100 duration=2s`가 `300 Kops/s` 이상
    `3회 연속` 통과
  - 구조적으로 막힌 지점을 코드와 측정값으로 분명하게 증명
- 위 셋 중 하나가 충족되기 전에는 “분석만 하고 중단”하지 않는다.

### 0.2 반복 루프

매 라운드는 아래 순서를 한 번 끝까지 돈다.

1. baseline 또는 직전 상태를 측정한다.
2. 병목 가설을 한 개 고른다.
3. 최소 수정으로 바로 실험한다.
4. 빌드한다.
5. `DEALER_ROUTER`, `SPOT_SENDSEND`, `SPOT_REQREP`를 같은 조건으로 다시 잰다.
6. 결과와 판단을 이 문서에 남긴다.
7. 다음 가설로 바로 넘어간다.

### 0.3 허용 작업 범위

- 필요하면 `git checkout` 또는 detached `HEAD`로 과거 커밋을 직접 재측정한다.
- 필요하면 Codex 서브 에이전트로 병목 아이디어 조사, 히스토리 비교,
  코드 경로 리서치를 병행한다.
- 다만 최종 코드 변경은 메인 작업 흐름에서 통합하고, 측정값으로 다시 검증한다.

### 0.4 측정 기준

- 주 기준 패턴:
  - `MULTI_DEALER_ROUTER`
  - `MULTI_SPOT_SENDSEND`
  - `MULTI_SPOT_REQREP`
- 주 기준 조건:
  - `tcp`
  - `64B`
  - `clients=100`
  - `duration=2s`
- `DEALER_ROUTER`가 정상 범위인지 먼저 확인한 뒤 `SPOT` 계열 수치를 해석한다.

### 0.5 우선순위

병목 가설은 아래 순서로 우선 검토한다.

1. direct route sender hot path
2. local target delivery queue/signal 제거
3. shared sender 구조
4. perf harness 의 sleep/poll 편향
5. req/rep bookkeeping

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
- `MULTI_SPOT_SENDSEND` 는 아직 정식 표에 없는 추가 비교 패턴이지만, 현재
  성능 개선 작업에서는 `MULTI_SPOT` / `MULTI_SPOT_REQREP` 와 같은 topology
  계약을 그대로 따른다.

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
- `clients=100, duration=2s`: 한 번은 `32.218 Kops/s`까지 회복됨
- 같은 방향의 코드 상태에서도 다시 `CLIENT_READY` failure 로 흔들림
  - [perf_c_multi_linux_20260423_090329.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_090329.txt)
- 최신 실험 상태에서는 `clients=1, duration=3s`가 다시 `0.333 ops/s`까지
  떨어진 실행도 있었다. 즉 현재 코드는 `80.667 ops/s`를 안정적으로 재현하는
  상태가 아니다.

정리하면, 현재는 **성능 부족**과 **경로 불안정**이 동시에 있다.
현재 handoff 기준으로 신뢰할 수 있는 결론은 아래 두 가지다.

- `SPOT` direct route는 아직 steady-state 로 안정화되지 않았다.
- `clients=100`에서 `CLIENT_READY` fail 이 다시 발생할 수 있다.

### 3.3 이번 라운드 추가 관측

- baseline 재확인:
  - `MULTI_DEALER_ROUTER tcp 64B, clients=100, duration=2s`:
    `451.654 Kops/s`
  - [perf_c_multi_linux_20260423_151736.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_151736.txt)
- direct route bind 대상을 `node_router`가 아니라 `peer_route_ingress`로 바로잡고,
  `SPOT_SENDSEND` 서버를 `1ms` sleep polling thread 대신
  `ROUTED_READABLE` dispatch event로 바꾼 뒤 한 번은 아래까지 올라갔다.
  - `MULTI_SPOT_SENDSEND tcp 64B, clients=100, duration=2s`:
    `37.355 Kops/s`
  - [perf_c_multi_linux_20260423_152149.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_152149.txt)
- 하지만 같은 방향의 코드에서도 run 마다 다시 크게 흔들렸다.
  - `MULTI_SPOT_SENDSEND tcp 64B`: `CLIENT_READY fail`
    - [perf_c_multi_linux_20260423_152648.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_152648.txt)
  - `ZLINK_ENABLE_SPOT_DIRECT_ROUTE=1` 상태에서 `MULTI_SPOT_SENDSEND tcp 64B`:
    `50 ops/s`
    - [perf_c_multi_linux_20260423_152700.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_152700.txt)
- `MULTI_SPOT_REQREP`는 `CLIENT_READY`에서 바로 죽던 상태에서
  실행은 끝까지 가는 쪽으로는 회복됐지만, 수치는 아직 매우 낮다.
  - `MULTI_SPOT_REQREP tcp 64B, clients=100, duration=2s`:
    `2.446 Kops/s`
  - [perf_c_multi_linux_20260423_152537.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_152537.txt)

이번 라운드 결론은 아래와 같다.

- direct route 수신 바인딩과 `SPOT_SENDSEND` 서버 수신 경로에 분명한 구조 오류가 있었다.
- 그 오류를 바로잡아도 `300 Kops/s`와는 아직 매우 멀다.
- 특히 direct route가 실제로 잡힌 run에서 `~50 ms` 지연이 관측되어,
  현재 sender hot path의 `POLLOUT 100 ms` 대기가 실제 RTT를 크게 올리고 있을
  가능성이 높다.

### 3.4 바로 다음 재측정과 실패한 가설

- 구조 수정만 남긴 baseline 재측정:
  - `MULTI_DEALER_ROUTER tcp 64B, clients=100, duration=2s`:
    `427.233 Kops/s`
    - [perf_c_multi_linux_20260423_154502.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_154502.txt)
  - `MULTI_SPOT_SENDSEND tcp 64B, clients=100, duration=2s`:
    `34.661 Kops/s`
    - [perf_c_multi_linux_20260423_154512.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_154512.txt)
  - `MULTI_SPOT_REQREP tcp 64B, clients=100, duration=2s`:
    `2.293 Kops/s`
    - [perf_c_multi_linux_20260423_154520.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_154520.txt)
- completion signal 을 “queue empty -> non-empty” 전환 때만 보내는 실험은
  `SPOT_REQREP`를 거의 올리지 못했다.
  - `MULTI_SPOT_REQREP tcp 64B, clients=100, duration=2s`:
    `2.333 Kops/s`
    - [perf_c_multi_linux_20260423_154704.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_154704.txt)
- `clients=1` 소형 재현에서는 아래처럼 거의 고정 `50~100 ms` 지연이 다시 보였다.
  - `MULTI_SPOT_REQREP tcp 64B, clients=1, duration=2s`:
    `50.829 ms`
    - [perf_c_multi_linux_20260423_155039.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_155039.txt)
  - `MULTI_SPOT_SENDSEND tcp 64B, clients=1, duration=2s`:
    `101.160 ms`
    - [perf_c_multi_linux_20260423_155251.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_155251.txt)
- direct route warmup `100 ms` 제거, reply `DONTWAIT`, direct route env gate 복구 실험도
  해 봤지만 `SPOT_SENDSEND`와 `SPOT_REQREP`를 함께 올리는 결과는 만들지 못했다.
  - mesh-only 성격으로 돌린 `MULTI_SPOT_SENDSEND tcp 64B, clients=100`:
    `36.422 Kops/s`
    - [perf_c_multi_linux_20260423_155611.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_155611.txt)
  - 같은 실험의 `MULTI_SPOT_REQREP tcp 64B, clients=100`:
    `2.457 Kops/s`
    - [perf_c_multi_linux_20260423_155618.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_155618.txt)

이번 묶음 실험이 남긴 결론은 아래와 같다.

- 현재 `REQREP` completion queue signal 자체는 주 병목으로 보기 어렵다.
- `clients=1`에서도 `50~100 ms`가 반복되므로, 동시성보다는 single-flow 경로의 고정
  대기나 benchmark dispatch 진행 타이밍을 먼저 확인해야 한다.
- direct route를 완전히 끄는 방향도 `SPOT_SENDSEND`를 `300 Kops/s`로 밀어 올릴
  해법은 아니었다.

### 3.5 문서 정렬 라운드

- 원래 문서 의도대로 remote routed 경로를 `node_router <-> node_router` 기준으로
  다시 맞추는 패치를 적용했다.
  - 외부 routed bind 를 `peer_route_ingress`가 아니라 `node_router`에 붙였다.
  - remote routed sender socket 을 `PAIR`가 아니라 `DEALER`로 바꿨다.
  - routed message 가 direct 실패 시 `mesh_pub/xsub`로 우회하던 분기를 제거했다.
  - node ingress 에 도착한 non-local routed envelope 을 다시 publish 하지 않고
    local node 에서만 처리하도록 막았다.
- 그 상태의 재측정 결과는 아래와 같다.
  - `MULTI_DEALER_ROUTER tcp 64B, clients=100, duration=2s`:
    `433.529 Kops/s`
    - [perf_c_multi_linux_20260423_161137.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_161137.txt)
  - `MULTI_SPOT_SENDSEND tcp 64B, clients=100, duration=2s`:
    `38.528 Kops/s`
    - [perf_c_multi_linux_20260423_161137.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_161137.txt)
  - `MULTI_SPOT_REQREP tcp 64B, clients=100, duration=2s`:
    `2.283 Kops/s`
    - [perf_c_multi_linux_20260423_161137.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_161137.txt)

이번 라운드 결론은 아래와 같다.

- 구조적으로는 이제 “routed 메시지를 `pub/sub`로 터널링한다”는 큰 어긋남을 끊었다.
- 하지만 throughput 은 거의 그대로라서, 남은 병목은 `mesh fallback`보다
  `node_router` 경유 hot path 또는 local final delivery 쪽에 더 가깝다.
- 즉 다음 라운드는 “경로 정렬”이 아니라 “direct path 안의 고정 비용”을 깎는
  작업으로 넘어가야 한다.

### 3.6 `rid` 문자열화/lookup 비용 축소

- routed hot path에서 `rid` 관련 중복 문자열화와 lookup key 합성을 줄였다.
  - `find_spot_state_by_identity()`의 내부 index 를
    `node_rid + '\n' + spot_rid` 단일 문자열 key 에서
    `node_rid -> spot_rid` 2단계 map 으로 바꿨다.
  - `start_spot_request_to_spot()`, `start_spot_request_to_router()`,
    `spot_reply_spot_impl()`, `spot_reply_router_impl()`,
    `start_router_request_to_spot()`, `router_reply_spot_impl()`,
    `router_send_spot_impl()`에서 같은 `routing_id_key()` 반복 호출을
    지역 문자열 1회 생성으로 줄였다.
- 그 상태의 재측정 결과는 아래와 같다.
  - `MULTI_DEALER_ROUTER tcp 64B, clients=100, duration=2s`:
    `445.849 Kops/s`
  - `MULTI_SPOT_SENDSEND tcp 64B, clients=100, duration=2s`:
    `38.760 Kops/s`
  - `MULTI_SPOT_REQREP tcp 64B, clients=100, duration=2s`:
    `2.384 Kops/s`
  - [perf_c_multi_linux_20260423_161819.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_161819.txt)
- 추가 확인:
  - direct sender 초기 settle `100 ms` 제거는 `clients=1` 고정 지연을 없애지 못했다.
  - `MULTI_SPOT_SENDSEND tcp 64B, clients=1, duration=2s`:
    `101.427 ms`
  - `MULTI_SPOT_REQREP tcp 64B, clients=1, duration=2s`:
    `50.671 ms`
  - [perf_c_multi_linux_20260423_161948.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_161948.txt)

이번 라운드 결론은 아래와 같다.

- `rid` 문자열화/lookup 비용은 줄였지만 개선폭은 작다.
- 즉 이 비용은 분명히 낭비이지만, 현재 `300 Kops/s`를 막는 주 병목은 아니다.
- `101 ms` / `50 ms` 고정 지연은 sender warmup sleep 이 아니라 그 이후 단계에 있다.

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

- 구조는 `node_router <-> node_router` 쪽으로 다시 맞췄지만 수치는 거의 안 올랐다.
- 즉 지금 병목은 “mesh fallback 때문에 느리다” 수준보다 더 안쪽에 있다.
- 여전히 sender hot path 의 `POLLOUT` 대기, `node_router` 경유 비용,
  local final delivery parse/dispatch 비용을 의심해야 한다.
- 현재 `peer_route_tx` 는 이름은 그대로지만 실제로는 remote `node_router`에 붙는
  node 단위 shared direct sender 다. multi-client 에서 이 공유 sender 구조를
  유지할지 다시 검토해야 한다.

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
- external routed bind 를 `node_router`로 정렬하고 routed -> mesh fallback 제거
  - [spot_data_plane_protocol.cpp](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_data_plane_protocol.cpp)
  - [spot_runtime.cpp](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_runtime.cpp)
  - [service_spot_request_reply_api.cpp](/home/hep7/project/kairos/zlink/core/src/api/service_spot_request_reply_api.cpp)
- `MULTI_SPOT_SENDSEND` 서버 수신을 dispatch event 기반으로 전환
  - [perf_multi_spot_sendsend_server.cpp](/home/hep7/project/kairos/zlink/bindings/c/perf/multi/src/perf_multi_spot_sendsend_server.cpp)

### 6.1 landed 변경과 실험 중 변경 구분

이 문서를 작성하는 시점의 워크트리 기준으로 아래 둘은 **실험 중 변경**이다.

- [spot_runtime.cpp](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_runtime.cpp)
- [service_spot_request_reply_api.cpp](/home/hep7/project/kairos/zlink/core/src/api/service_spot_request_reply_api.cpp)

즉 다른 작업자는 이 두 파일의 현재 내용을 “확정된 개선”으로 보지 말고,
실험 상태로 간주하고 다시 검증해야 한다.

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

### 8.1 direct route hot path

- 이제 routed -> mesh fallback 은 끊었지만 `SPOT_SENDSEND`는 여전히 `~38 Kops/s`다.
- 따라서 현재는 direct route “존재 여부”보다 direct route “고정 비용”을 봐야 한다.
- 특히 hot path의 `wait_socket_events_internal(..., POLLOUT, 100)`와
  `node_router` 수신 후 local final delivery parse/dispatch 비용을 먼저 계측해야
  한다.

### 8.2 `rid` 비용 다음 후보

- `rid` 문자열화/lookup 축소는 소폭 개선만 만들었다.
- 다음은 아래 두 항목을 우선 본다.
  - receiver 쪽 `parse_spot_routed_envelope()` 후
    `std::string 4개 materialize -> routing_id_from_string_local()` 왕복
  - direct path steady-state 에서도 매 send 마다 들어가는
    `wait_socket_events_internal(..., POLLOUT, 100)` 대기

### 8.2 shared sender 구조

- 현재 direct route sender 는 node 단위 공유 소켓이다.
- multi-client 에서 이 공유 sender 를 계속 쓸지, per-target 또는 per-worker 로
  나눌지 결정이 필요하다.
- 현재는 send lock 을 넣었지만, 그것만으로 충분한지는 아직 확인되지 않았다.

### 8.3 local recv surface 최종 단순화

- 지금도 일부 경계에서 legacy dispatch/queue 의미가 남아 있다.
- 최종적으로는 target spot recv surface가 실제 최종 수신면이 되도록 더 줄여야
  한다.

### 8.4 benchmark 진행 타이밍

- `clients=1`에서 `SPOT_SENDSEND ~101 ms`, `SPOT_REQREP ~50 ms`가 반복된다.
- 이 수치는 data path 자체뿐 아니라 perf harness 의 active loop, dispatch wakeup,
  hidden completion drain 타이밍 중 하나가 fixed delay 로 작동할 가능성을 시사한다.
- 다음 라운드는 `run_benchmarks_multi.sh`를 우회해 client/server 바이너리를 직접
  실행하고 stderr 로그까지 같이 봐야 한다.

## 9. 다음 작업 순서

1. direct path 안의 고정 비용을 먼저 계측한다.
   - `clients=1`과 `clients=100`에서 `POLLOUT` 대기, envelope parse,
     local final delivery 구간을 나눠 본다.
   - `run_benchmarks_multi.sh` 우회 실행과 stderr 로그를 함께 본다.

2. receiver 쪽 envelope parse 와 local final delivery 왕복 비용을 줄인다.
   - parse 결과를 다시 `std::string -> zlink_routing_id_t`로 되돌리는 구간을
     우선 본다.
   - 가능하면 local delivery 내부 fast path 로 바로 넘긴다.

3. shared direct sender 구조를 다시 검토한다.
   - node 단위 shared sender 유지
   - worker 단위 sender
   - target endpoint 단위 sender
   세 방향을 비교

4. `SPOT_SENDSEND` 를 먼저 올린다.
   - `SPOT_REQREP` 보다 `SPOT_SENDSEND` 가 먼저 올라야 한다.
   - `SPOT_SENDSEND` 가 오르지 않으면 문제는 req/rep 계층이 아니다.

5. `SPOT_SENDSEND` 가 안정화되면 그 다음 `SPOT_REQREP` 를 다시 잰다.

## 10. 감독 이력

이 섹션은 자동화 감독 루프가 실행한 작업과 perf 결과를 순서대로 기록한다.
각 이터레이션마다 작업 내용, 빌드/테스트 결과, 다음 판단을 남긴다.

| 이터레이션 | 날짜       | 작업 요약 | 관측 결과 | 판단 |
|-----------|-----------|-----------|-----------|------|
| 0 | 2026-04-23 | local queue/signal 비용 축소 후 baseline 확인 | `45~55 Kops/s`, reqrep와 sendsend 차이 작음 | 병목은 req/rep보다 routed path 쪽 |
| 1 | 2026-04-23 | direct route sender warmup/ready-after window 추가 | `clients=1`에서 한 번 `80.667 ops/s` 회복 | direct path 방향은 맞지만 안정화 미완료 |
| 2 | 2026-04-23 | shared sender/send lock/ready window 조정 | `clients=100`에서 `32.218 Kops/s`와 `CLIENT_READY fail`이 번갈아 발생 | 경로 안정성 우선 필요 |
| 3 | 2026-04-23 | `node_router <-> node_router` direct path 실험 계속 | 최신 상태에서도 `clients=1` 재현 불안정, `clients=100` partial fail 재발 | 다음 작업자는 안정화부터 다시 시작해야 함 |
| 4 | 2026-04-23 | direct route bind를 `peer_route_ingress`로 수정하고 `SPOT_SENDSEND` 서버를 dispatch event 기반으로 전환 | `SPOT_SENDSEND`가 한 번 `37.355 Kops/s`까지 회복됐지만 같은 코드에서 다시 `CLIENT_READY fail`, `50 ops/s`도 관측. `SPOT_REQREP`는 `2.446 Kops/s`로 실행 완료 | 구조 오류 하나는 잡았지만 hot path와 안정성 문제는 여전히 남음 |
| 5 | 2026-04-23 | completion signal 축소, reply `DONTWAIT`, direct-route warmup 제거, env gate 복구까지 차례로 실험 | `SPOT_REQREP`는 `2.293 -> 2.333 -> 2.457 Kops/s` 범위, `SPOT_SENDSEND`는 `34.661 -> 36.422 Kops/s` 수준. `clients=1`에서는 `SPOT_REQREP ~50 ms`, `SPOT_SENDSEND ~101 ms` 고정 지연 재현 | completion queue 자체보다 single-flow 고정 대기 또는 benchmark 진행 타이밍을 먼저 의심해야 함. 효과 없는 실험은 되돌리고 직접 바이너리 로그 분석으로 넘어가야 함 |
| 6 | 2026-04-23 | 문서 의도대로 external routed bind를 `node_router`에 다시 붙이고, `PAIR -> DEALER`, routed -> mesh fallback 제거 | `DEALER_ROUTER 433.529 Kops/s`, `SPOT_SENDSEND 38.528 Kops/s`, `SPOT_REQREP 2.283 Kops/s` | 구조 어긋남은 줄였지만 throughput 변화는 미미했다. 다음 라운드는 `mesh`가 아니라 direct path 내부 고정 비용 계측으로 넘어가야 함 |
| 7 | 2026-04-23 | `rid` 중복 문자열화와 spot identity lookup key 합성을 축소 | `DEALER_ROUTER 445.849 Kops/s`, `SPOT_SENDSEND 38.760 Kops/s`, `SPOT_REQREP 2.384 Kops/s`. `clients=1`에서는 settle `100 ms` 제거를 따로 확인했지만 `SPOT_SENDSEND ~101.427 ms`, `SPOT_REQREP ~50.671 ms` 그대로 | `rid` 비용은 낭비지만 주 병목은 아니다. 다음은 receiver parse/materialize 왕복과 `POLLOUT` 대기를 더 직접 봐야 함 |
| 8 | 2026-04-23 | perf가 실제로 `core/build/libzlink.so`를 링크한다는 점을 확인하고, 그 빌드에 문서 정렬 변경을 실제 반영한 뒤 direct route 계측을 추가 | `core/build` 반영 뒤 `SPOT_REQREP`는 기존 `2.384 Kops/s`가 아니라 아예 `FAIL`로 바뀌었다. 고정 포트 재현에서 client direct route target이 처음에는 `55184`처럼 잘못 계산됐고, `single_peer_route_endpoint()`를 `active_endpoints` 기준으로 바꾼 뒤에는 `55000`으로 바로잡혔다. 그 뒤에도 request는 long retry 후 `send rc=0`까지 가지만 reply `0건`, `size failed=64 err=11`로 끝난다. `ss` 관측에서는 route listener는 뜨지만 direct route 연결은 안정적으로 유지되지 않았다 | 기존 perf 수치는 실제 최신 core 구조가 아니라 예전 `core/build` 기준이 섞여 있었다. 지금 드러난 핵심은 `node_router` direct path가 실제 런타임에서 아직 완성되지 않았다는 점이다. 다음 라운드는 `peer_route_tx DEALER -> remote node_router` 연결 성립 자체와 server side recv 진입 여부를 먼저 복구해야 한다 |
| 9 | 2026-04-23 | external direct route recv를 socket message dispatch로 우회 복구하고, `SPOT_REQREP` client active loop를 `waiting_reply` 전수 progress sweep 대신 poller 기반으로 정리했다. spot request timeout은 `request_timeout::schedule/cancel` per request 대신 state-local deadline scan으로 바꿨다 | 새 direct-route smoke 두 개는 모두 통과했다. `SPOT_SENDSEND tcp 64B clients=100 duration=2s`는 `132.905 Kops/s`, 다시 확인하면 `136.734 ~ 139.670 Kops/s`가 나왔다. `SPOT_REQREP`는 기본 `rcvtimeo=200`에서 `48.836 -> 69.906 -> 95.989 Kops/s`까지 올라왔고, `rcvtimeo=5000` 조건에서는 `run 3/3`에서 `101.630 Kops/s`를 한 번 넘겼다 | root cause는 direct-route ingress 불능과 `REQREP` 전용 control-plane 비용이 겹친 것이었다. 특히 `timeout task lifecycle`와 client-side progress timing이 throughput cap의 핵심이었다. 아직 `REQREP 100 Kops/s`가 기본 `rcvtimeo=200`에서 안정 통과한 상태는 아니므로, 다음 라운드는 timeout 경로 추가 절감과 reply completion fast path를 계속 줄여야 한다 |
| 10 | 2026-04-23 | `SPOT_REQREP` poller event 처리에서 같은 batch 안의 slot `POLLIN` 즉시 progress와 completion signal 이후 waiting slot 전체 progress가 중복되던 부분을 정리했다. completion signal이 있으면 waiting slot 전체를 한 번만 progress하고, signal이 없을 때만 readable slot 개별 progress를 탄다 | direct-route smoke 두 개는 계속 통과했다. `SPOT_REQREP tcp 64B clients=100 duration=2s` 단독 기본값은 [perf_c_multi_linux_20260423_192337.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_192337.txt)에서 `95.926 Kops/s`로 다시 `95 Kops/s`를 넘겼다. `SPOT_SENDSEND tcp 64B clients=100 duration=2s` 단독 기본값은 [perf_c_multi_linux_20260423_192406.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_192406.txt)에서 `130.342 Kops/s`였다. 다만 combined run은 [perf_c_multi_linux_20260423_192349.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_192349.txt)에서 `SPOT_REQREP 97.960 Kops/s`와 함께 `SPOT_SENDSEND 45.214 Kops/s`로 크게 흔들려, cross-pattern 간섭 또는 runner 순서 영향을 더 확인해야 한다. 이터레이션 중 병렬로 잘못 실행한 perf 결과 [perf_c_multi_linux_20260423_191838.txt](/home/hep7/project/kairos/zlink/bindings/c/perf/results/multi/report/perf_c_multi_linux_20260423_191838.txt)는 폐기했다 | 이번 수정은 `REQREP` 쪽 `95 Kops/s` 회복에는 유효했다. 반면 `SENDSEND`는 단독 측정에서는 유지되지만 combined run에서 흔들려서, 다음 라운드는 `SPOT_SENDSEND`를 `REQREP`와 분리한 상태에서 먼저 안정화하고 그 다음 combined 순서/runner 간섭을 따져야 한다 |

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
