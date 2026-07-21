# Core 10.0 Spot 데이터 경로 성능 개선 목표

> 이 문서는 Core 10.0의 Spot 성능을 정확히 측정하고, 확인된 병목을 개선하기 위한
> 목표와 완료 조건을 정의한다.
>
> 이 문서는 공개 API 계약이나 RouteMesh 10.0 전환 진행표가 아니다. 구현 방법과
> 수정 순서는 측정 결과에 따라 결정한다. 조사 과정과 실험 결과는
> `doc/perf/perf/core/log/` 아래 작업 로그에 기록한다.

## 1. Goal

다음 문장을 그대로 goal로 사용할 수 있다.

```text
Core 10.0의 C multi Spot 성능 측정이 실제 데이터 경로의 처리량과 지연을 정확히
나타내도록 검증하고, TLS/WSS 실패와 pub/sub 지연 회귀를 해결하며, Spot 데이터
경로가 동일 조건의 ROUTER 기준 성능에 근접하도록 개선한다.

완료 조건:
1. MULTI_SPOT_PUBSUB, MULTI_SPOT_REQREP, MULTI_SPOT_SENDSEND의 모든 지정
   transport와 payload 조합이 실패 없이 반복 측정된다.
2. 측정 시간, 송수신 건수, 지연 표본, drain 범위와 집계 계산이 검증 가능한
   기준 입력에서 정확함이 증명된다.
3. SPOT_PUBSUB은 동일한 one-to-many ROUTER 기준 처리량의 90% 이상을 달성한다.
4. SPOT_REQREP와 SPOT_SENDSEND는 각각 대응 ROUTER 기준 처리량의 90% 이상을
   달성한다.
5. Spot 지연과 일반 MULTI_PUBSUB 지연이 이 문서의 목표 범위에 들어오며,
   이를 위해 처리량이나 수신 완전성을 희생하지 않는다.
6. 공개 계약이나 시험 조건을 완화하는 우회 없이 관련 test와 3회 연속 full
   perf를 통과한다.
```

## 2. 문제와 시작 기준

### 2.1 측정 snapshot

| 항목 | 값 |
|------|----|
| source commit | `57fa7ed956ce` |
| 문제 report | `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260719_161403.txt` |
| 과거 보존 기준 | `bindings/c/perf/baseline/perf_c_multi_linux_20260616_081700_final_retained_spot_multi_full_baseline_20260616.txt` |
| 실행 환경 | WSL2, Intel Core Ultra 7 265K, 20 cores |
| 현재 조건 | clients 100, duration 5초, runs 1 |
| Spot 구성 | child process 100개, process마다 MeshNode 1개와 entry Spot 1개, I/O thread 1개 |

문제 report는 `success: 190`, `fail: 42`, `status: partial`이다. 실패가 포함된 단일
실행이므로 최종 기준으로 사용할 수는 없다. 다만 correctness 결함과 큰 성능 차이를
보여 주는 시작 red gate로 사용한다.

### 2.2 확인된 증상

64바이트 결과에서 Spot echo 처리량은 대응 ROUTER↔ROUTER 처리량과 큰 차이가 있다.

| transport | Spot req/rep | ROUTER req/rep | 비율 | Spot send/send | ROUTER send/send | 비율 |
|-----------|-------------:|---------------:|-----:|---------------:|-----------------:|-----:|
| tcp | 29.158 Kops/s | 212.380 Kops/s | 13.7% | 27.363 Kops/s | 431.440 Kops/s | 6.3% |
| tls | FAIL | 215.098 Kops/s | 판정 불가 | FAIL | 371.481 Kops/s | 판정 불가 |
| ws | 28.814 Kops/s | 236.887 Kops/s | 12.2% | 28.622 Kops/s | 389.554 Kops/s | 7.3% |
| wss | FAIL | 212.905 Kops/s | 판정 불가 | FAIL | 358.440 Kops/s | 판정 불가 |

`MULTI_SPOT_PUBSUB` 64바이트 처리량은 tcp `298.548 Kmsg/s`, ws
`298.495 Kmsg/s`이며 tls와 wss는 실패한다. 현재 report에는 Spot pub/sub과 같은
방향 및 peer 구성으로 측정한 ROUTER 단방향 기준이 없다.

일반 `MULTI_PUBSUB` tcp 64바이트는 보존 기준보다 처리량은 높지만 지연이 크게
증가했다.

| metric | 2026-06-16 기준 | 2026-07-19 문제 report | 변화 |
|--------|-----------------:|-----------------------:|-----:|
| throughput | 2.449 Mmsg/s | 3.477 Mmsg/s | +42.0% |
| p95 | 334.205 ms | 2336.511 ms | 7.0배 |
| p99 | 430.618 ms | 6458.400 ms | 15.0배 |

이 결과가 실제 queue 적체인지, phase·timestamp·drain 계산 문제인지 먼저 구분해야
한다. 처리량 상승만으로 개선을 판정하지 않는다.

### 2.3 코드 검토에서 찾은 조사 근거

다음은 구현 방향을 강제하는 목록이 아니다. 실제 수정 전에 재현, test, profile로
원인 여부를 확인해야 한다.

- `core/src/api/mesh/mesh_api.cpp`의 `set_tls_server()`와 `set_tls_client()`는
  입력을 검증하지만 TLS 설정을 MeshNode에 보존하지 않는다.
  `core/src/runtime/services/mesh/mesh_wire.cpp`의 `wire_start()`에서도 이 설정을
  내부 ROUTER에 적용하는 코드를 확인할 수 없다. tls·wss 실패와의 인과관계를
  작은 integration test로 확인한다.
- ws `MULTI_SPOT_REQREP`는 64바이트 측정 뒤
  `core/src/runtime/sockets/internal/fq.cpp:15`의 `_pipes.empty()` assertion으로
  종료한다. MeshNode 종료와 pipe 정리 과정의 correctness 문제인지 확인한다.
- `MULTI_SPOT_SENDSEND` tcp 131072바이트는 앞선 payload 실행 뒤 server가
  종료한다. 크기 전환, backpressure, 종료 상태 중 어느 조건이 원인인지 분리한다.
- Spot perf hot loop에는 메시지 생성과 payload 초기화, 문자열 생성, 즉시 재시도
  같은 비용 후보가 있다. allocation count와 CPU profile로 실제 비중을 확인한다.
- publish 데이터 경로에는 peer·subscription 검색, target 목록 구성, envelope 및
  frame 생성, 복사, mutex 대기, mailbox admission 비용 후보가 있다. harness 비용을
  제거한 뒤에도 목표에 미달할 때 profile 결과를 기준으로 우선순위를 정한다.

## 3. 비교 기준

### 3.1 Spot pub/sub의 주 비교 기준

90% 목표는 유지한다. 다만 방향이 다른 `DEALER → ROUTER` 단방향 결과를 유일한
분모로 사용하지 않는다.

Spot pub/sub의 remote 구간은 hub에서 100개 peer로 같은 payload를 전송하는
one-to-many 흐름이다. `DEALER → ROUTER`는 many-to-one 흐름이므로 routing ID 선택,
target별 전송, HWM과 fan-out 비용이 다르다.

따라서 Spot pub/sub의 주 비교 기준은 같은 방향과 자원 조건을 가진
`MULTI_ROUTER_ROUTER_ONEWAY`로 한다. 현재 이 패턴이 없으므로 정확한 비교에 필요한
최소 범위로 추가한다.

| 조건 | Spot과 ROUTER 기준에서 일치시킬 값 |
|------|------------------------------------|
| 구성 | hub 1개, peer process 100개 |
| peer 자원 | process마다 context 1개, ROUTER 또는 MeshNode 1개, I/O thread 1개 |
| 전송 방향 | hub에서 100개 peer로 전송 |
| 수신 건수 | 모든 peer가 받은 active record의 합 |
| payload | metric header를 포함한 같은 byte 수 |
| phase | 같은 ready, active, cooldown 및 drain 의미 |
| transport | tcp, tls, ws, wss |
| queue 조건 | 같은 HWM 정책과 OS buffer 조건 |

`MULTI_DEALER_ROUTER_ONEWAY`는 보조 진단값으로 사용할 수 있다. 주 비교 기준과의
차이가 반복 측정 오차 안에 있다는 증거가 없으면 대체 분모로 사용하지 않는다.

### 3.2 req/rep와 send/send 비교 기준

`MULTI_SPOT_REQREP`는 `MULTI_ROUTER_ROUTER_REQREP`와 비교하고,
`MULTI_SPOT_SENDSEND`는 `MULTI_ROUTER_ROUTER_SENDSEND`와 비교한다. 두 패턴도
peer 수, transport, payload, I/O thread, HWM, phase와 집계 단위가 같은지 먼저
확인한다. 조건 차이가 발견되면 비율을 계산하기 전에 기준 시험을 바로잡는다.

## 4. 측정 계약

구현 방법은 실험 결과에 따라 선택하지만 다음 조건은 성능 수치의 의미를 지키기 위해
고정한다.

- Core 변경 뒤에는 `core/build`를 다시 만들고 runner가 출력한 실제
  `libzlink.so`가 최신 source로 만들어졌는지 확인한다.
- 같은 source와 머신에서 ROUTER 기준과 Spot을 인접하게 실행한다. 실행 순서의 영향을
  줄이기 위해 반복마다 순서를 교차한다.
- 정식 판정은 cell마다 5회 paired run의 median을 사용한다.
- 최종 반복은 100 peer, 5초 active duration, 지정된 transport와 payload를 사용한다.
- warm-up, active, cooldown, drain과 latency timestamp의 시작·종료 의미가 비교
  패턴에서 같아야 한다.
- throughput의 송수신 건수와 시간 분모, bandwidth 방향 계수, mean·p95·p99 표본
  선택과 reservoir 가중치를 deterministic input으로 검증한다.
- fail, skip, unsupported 또는 필수 metric 누락이 있는 cell은 성능 판정에서
  제외하지 않고 실패로 처리한다.
- peer 수, I/O thread, payload, HWM, 인증 검증, 수신 건수 또는 latency 표본을
  줄여 목표를 맞추지 않는다.
- perf 전용 Core 분기나 공개 API 우회 경로를 추가하지 않는다.

2명과 10명의 peer 또는 짧은 duration은 재현과 진단 속도를 높이기 위한 smoke에만
사용할 수 있다. 완료 수치는 반드시 정식 조건에서 다시 측정한다.

## 5. 정량 목표

### 5.1 안정성과 측정 정확성

| ID | 목표 | 완료 기준 |
|----|------|-----------|
| R1 | Spot transport 정상화 | Spot 3패턴 × 4 transport × 6 payload의 72 cell이 `fail=0`, `status=complete` |
| R2 | 반복 안정성 | 같은 source와 조건의 full perf 3회 연속 complete |
| R3 | lifecycle | 실행 뒤 남은 child process 0개, assertion 0개, shutdown timeout 0개 |
| R4 | memory safety | 관련 sanitizer error와 leak 0개 |
| R5 | metric 정확성 | 고정 입력에 대한 count, duration, mean, p95, p99 계산 test 통과 |

### 5.2 Spot 처리량

모든 비율은 cell별 paired run 5회의 median으로 계산한다.

| Spot 패턴 | 비교 기준 | 완료 조건 |
|-----------|-----------|-----------|
| `MULTI_SPOT_PUBSUB` | `MULTI_ROUTER_ROUTER_ONEWAY` | `Spot throughput / ROUTER throughput >= 0.90` |
| `MULTI_SPOT_REQREP` | `MULTI_ROUTER_ROUTER_REQREP` | `Spot throughput / ROUTER throughput >= 0.90` |
| `MULTI_SPOT_SENDSEND` | `MULTI_ROUTER_ROUTER_SENDSEND` | `Spot throughput / ROUTER throughput >= 0.90` |

현재 ROUTER echo 결과에서 계산한 64바이트 시작 red gate는 다음과 같다. 이는 문제
snapshot의 비교값이며 최종 paired 기준을 대신하지 않는다.

| transport | Spot req/rep 최소 | Spot send/send 최소 |
|-----------|---------------------:|-----------------------:|
| tcp | 191.142 Kops/s | 388.296 Kops/s |
| tls | 193.588 Kops/s | 334.333 Kops/s |
| ws | 213.198 Kops/s | 350.599 Kops/s |
| wss | 191.615 Kops/s | 322.596 Kops/s |

Spot pub/sub은 방향이 일치하는 ROUTER 기준을 추가한 뒤 절대 목표값을 확정한다.
참고용 `DEALER_DEALER` 수치에 90%를 곱해 목표를 미리 확정하지 않는다.

### 5.3 latency와 일반 pub/sub 회귀

| 대상 | 완료 조건 |
|------|-----------|
| Spot 3패턴 | 각 cell의 mean, p95, p99가 대응 ROUTER 기준의 1.25배 이하 |
| 일반 `MULTI_PUBSUB` | 처리량을 paired 시작 기준의 90% 이상 유지 |
| 일반 `MULTI_PUBSUB` | 각 cell의 p95와 p99가 보존 기준과 paired 시작 기준 중 더 낮은 값의 1.10배 이하 |

일반 pub/sub tcp 64바이트에는 시작 문제를 명확히 닫기 위한 절대 red gate도 적용한다.

| metric | 완료 하한 또는 상한 |
|--------|--------------------:|
| throughput | 3.130 Mmsg/s 이상 |
| p95 | 367.626 ms 이하 |
| p99 | 473.680 ms 이하 |

처리량을 의도적으로 제한하거나 latency 표본을 버려 지연 목표만 맞추면 실패다. 송신
건수, 수신 건수, drop 수와 active 종료 시 남은 queue를 진단하여 두 지표가 함께
개선됐음을 설명해야 한다.

### 5.4 회귀 보호

- SPOT 외 C multi 공통 패턴의 paired median 처리량이 5% 넘게 하락하지 않는다.
- public API 의미, error, ownership, multipart framing과 partial multicast
  admission을 바꾸지 않는다.
- TLS 인증서와 hostname 검증을 비활성화하지 않는다.
- 성능 효과보다 복잡성 및 회귀 위험이 큰 변경은 최종 결과에 남기지 않는다.

## 6. 조사와 개선 지침

다음 내용은 권장 순서이며 완료 조건이 아니다. 증거가 더 강한 원인이 발견되면 순서와
방법을 바꿀 수 있다.

1. 실패하는 TLS/WSS, WS 종료 assertion과 큰 payload 종료를 작은 조건에서
   재현하고 correctness 문제부터 분리한다.
2. 측정 계산을 고정 입력으로 검증하고, Spot과 ROUTER 기준의 조건이 같은지 확인한다.
3. allocation count와 CPU profile로 perf harness 비용과 Core 비용을 구분한다.
4. harness가 측정 대상이 아닌 비용을 추가한다면 공개 API와 ownership 계약 안에서
   제거한다.
5. 방향이 일치하는 ROUTER one-way 기준을 추가하고 90% 차이의 실제 크기를 확정한다.
6. 목표 미달 cell을 profile하여 가장 큰 Core 병목부터 개선한다.
7. 비자명한 Core 변경은 두 가지 이상 설계를 비교하고, 공개 interface를 늘리지
   않으면서 데이터 경로 내부에 복잡성을 가두는 쪽을 선택한다.
8. 각 의미 있는 변경 뒤에 correctness test와 paired perf를 실행하여 효과와 인접
   패턴 회귀를 기록한다.

문서에 적힌 병목 후보를 확인 없이 수정하거나, 체크리스트를 채우기 위해 효과가 없는
변경을 남기지 않는다. 반대로 현재 목록에 없는 원인도 재현과 측정으로 증명되면 우선
처리할 수 있다.

## 7. Goal 체크리스트

### 측정 신뢰성

- [ ] 실제 사용한 source commit, `libzlink.so`, 실행 옵션과 환경을 report에서
  확인할 수 있다.
- [ ] throughput, bandwidth와 latency 계산을 deterministic test로 검증했다.
- [ ] 비교 패턴의 구성, 전송 방향, 자원, phase와 집계 단위가 일치한다.
- [ ] 5회 paired median 비교가 cell별로 자동 판정된다.

### correctness

- [ ] Spot의 72개 cell이 모두 성공한다.
- [ ] TLS/WSS가 인증 검증을 유지한 상태로 성공한다.
- [ ] assertion, 비정상 종료, timeout과 남은 child process가 없다.
- [ ] 관련 regression test와 sanitizer 검증이 통과한다.

### 성능

- [ ] Spot pub/sub의 모든 cell이 일치하는 ROUTER one-way 기준의 90% 이상이다.
- [ ] Spot req/rep의 모든 cell이 ROUTER req/rep 기준의 90% 이상이다.
- [ ] Spot send/send의 모든 cell이 ROUTER send/send 기준의 90% 이상이다.
- [ ] Spot mean, p95와 p99가 대응 기준의 1.25배 이하다.
- [ ] 일반 pub/sub 처리량과 p95·p99 목표를 함께 만족한다.
- [ ] SPOT 외 공통 패턴에 5%를 넘는 처리량 회귀가 없다.

### 최종 증거

- [ ] 관련 build, test와 sanitizer 결과를 작업 로그에 기록했다.
- [ ] 변경 전후 profile과 cell별 paired 비교표를 작업 로그에 기록했다.
- [ ] C multi full perf가 3회 연속 `status=complete`다.
- [ ] 목표를 맞추기 위한 시험 조건 완화나 perf 전용 우회가 없음을 확인했다.
- [ ] 남은 위험이나 목표 미달 항목을 숨기지 않고 기록했다.

## 8. 작업 기록

각 조사와 개선 라운드는 다음 위치에 짧게 기록한다.

```text
doc/perf/perf/core/log/YYYY-MM-DD-spot-route-round-N-short-topic.ko.md
```

로그에는 가설, 재현 조건, 변경 전후 측정값, profile 또는 test 근거, 변경 내용,
회귀 확인과 다음 판단을 남긴다. 사용한 명령 전체를 정해진 형식에 맞추는 것보다
다른 실행자가 같은 결과를 재현하고 판단 근거를 확인할 수 있게 기록하는 것이
중요하다.

## 9. 완료와 목표 변경

§7 체크리스트와 §5의 cell별 정량 목표를 모두 만족할 때 완료한다. 평균값으로 실패
cell을 숨기거나, 일부 transport와 payload만 성공한 상태는 완료가 아니다.

90% 목표가 구조적으로 불가능하다고 판단해도 작업 중 임의로 낮추지 않는다. 방향이
일치하는 ROUTER 기준, profile, 공개 계약상 제거할 수 없는 비용과 두 가지 이상 설계
대안을 근거로 차이를 설명하고 사용자 검토를 받은 뒤 목표 변경 여부를 결정한다.
