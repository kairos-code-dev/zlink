# zlink Single Performance Test Policy

> **적용 범위**: zlink 전체 (core + bindings) — single-client 벤치마크
> **Policy Version**: 2.0
> **Date**: 2026-04-07
> **Scope**: `perf/single` 성능 테스트 정책
>
> 본 정책은 `perf/single`의 C++ 벤치마크와 in-repo single perf 자산이 존재하는
> 바인딩에 동일한 기준으로 적용한다.
> 단, 각 언어의 구현 완성도와 지원 패턴 범위는 다를 수 있으므로 실제 parity
> 수준은 언어별로 점검/정렬 대상이 된다.
>
> 언어별 적용 범위는 [PERF_POLICY.md](PERF_POLICY.md) 상단을 참조한다.
>
> **상위 문서**: [PERF_POLICY.md](PERF_POLICY.md) — 공통 원칙, 디렉터리 구조,
> RESULT 형식, 결과 저장, 출력 형식, 실패 처리, 환경 변수(공통), 리팩토링 원칙
>
> **관련 문서**: [PERF_MULTI_TEST_POLICY.md](PERF_MULTI_TEST_POLICY.md)
>
> 본 문서는 single suite **전용** 정책만 기술한다.
> 양 suite에 공통으로 적용되는 규칙은 상위 문서에서 관리한다.

---

## 1. Single 핵심 정책

| 항목 | 기준 |
|------|------|
| 측정 모델 | ready + active(duration) |
| throughput | one-way: `active 수신 건수 / active 시간(초)` (msg/s), echo: `active RTT 완료 수 / active 시간(초)` (ops/s) |
| latency | active 구간 수신 payload header timestamp 기반 |
| 대표값 | runs > 1일 때 metric별 median |
| 저장 경로 | `perf/results/single/report/` 단일 |

- 목적: 단일 소켓 경로에서 throughput, bandwidth, latency를 측정한다.
- 같은 active 구간에서 동일 메시지 집합으로 latency도 함께 집계한다.
- cpu/mem은 single 기본 perf surface와 RESULT 계약에 포함하지 않는다.
- `single`의 공식 lifecycle은 `ready -> active`다.
- size 변경 시마다 별도 프로세스로 실행하여 케이스 간 메트릭 오염을 방지한다.
- ready bool/count를 복사하기 위한 별도 state struct, heap alloc, mutex/cv 계층은
  만들지 않는다.
- 한 줄 요약: `single = ready + active`

### 1.1 I/O 모델 (recv only)

- **recv 모델**만 허용한다.
- `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`,
  `ROUTER_ROUTER`, `SPOT` 전부 recv only다.

#### 프로세스/스레드 모델

single은 **단일 프로세스** 안에서 sender와 receiver를 구동한다.
모든 single 패턴은 one-way 측정 surface를 사용한다.

**raw one-way 패턴** (PAIR, PUBSUB, DEALER_DEALER, DEALER_ROUTER, ROUTER_ROUTER):
```
┌─ process ────────────────────────────┐
│  sender thread      recv thread      │
│  blocking send ──►  poller POLLIN    │
│  (연속)             recv drain       │
│                     metric 집계      │
└──────────────────────────────────────┘
```
- sender thread: blocking send 연속 수행. HWM 도달 시 자연 backpressure.
- recv thread: poller `POLLIN` → `zlink_recv()` DONTWAIT drain 루프.
  throughput/latency 집계를 recv drain 안에서 인라인 수행.

**SPOT one-way 패턴**:
```
┌─ process ────────────────────────────┐
│  sender thread      recv thread      │
│  publish loop ───►  poller POLLIN    │
│                     recv drain       │
│                     metric 집계      │
└──────────────────────────────────────┘
```
- sender thread는 ready barrier 통과 후 metric header가 포함된 payload를 연속 publish한다.
- recv thread는 local probe barrier를 닫은 뒤 active payload만 집계한다.

**공통**:
- `EAGAIN` 기반 pending 관리, send-ready handler 등 multi에서 사용하는
  backpressure 메커니즘은 single에 적용하지 않는다.
- latency sample 계산, percentile sample 축적은 recv 루프 내에서 처리한다.
- phase 종료 보장은 내부 processed-count drain으로 처리한다.

### 1.2 실행 계약 불변식

- `single`의 최소 측정 단위는 `pattern/transport/size/run` 이다.
- runner는 size마다 perf 바이너리를 **다시 실행**해야 한다.
- 하나의 perf 바이너리 프로세스가 여러 size를 내부 루프로 순회하면 정책 위반이다.
- perf 바이너리는 해당 size 케이스를 측정하고 `RESULT` line만 출력한다.
- size 반복 실행, runs 집계, markdown table 출력, 결과 파일 저장은 runner 책임이다.
- single 리팩토링은 위 책임 분리를 유지해야 하며, 변경 시 자동 검증(test)도
  함께 갱신해야 한다.

### 1.3 금지 단계/개념

`single`에서 아래 단계/개념은 새로 만들지 않는다.

- `preflight`
- `prime`
- `settle`
- `stable`
- `quiet`
- `idle drain`
- `expected_ready_count > 1`

위 항목이 이미 존재하지만 실제로는 ready 이벤트 하나 대기하거나 phase 종료를
우회적으로 표현한 것뿐이면 삭제한다.

> 공통 금지 단계(`quiescent` 등)는 [PERF_POLICY.md § 1.1](PERF_POLICY.md) 참조.

---

## 2. Phase 규칙

```text
[single phase]: [ready] -> [active(duration)]
```

| Phase | 방식 | 기본값 | 환경 변수 |
|-------|------|--------|-----------|
| ready | event-based | raw=`CONNECTION_READY`, SPOT=local probe-based barrier | `PERF_CONNECT_READY_TIMEOUT_MS` 계열 timeout |
| active | time-based | 5s | `PERF_SINGLE_DURATION_SECONDS` |

- `setup_connected_pair()`는 내부적으로 low-cost monitoring ready gate를
  캡슐화한 helper인 경우에만 허용된다. 별도/독자적인 start gate 규칙으로
  취급하지 않는다.
- pattern별 low-cost ready event는 single 측정의 공식 start gate다. benchmark
  시작 전 준비 판정은 monitoring event로 해결하고, perf 파일 안의 커스텀
  handshake loop, sleep, monitor snapshot polling으로 대체하지 않는다.
- 패턴 파일에서는 공통 helper를 통해 `wait_*ready*()` 형태로 감싸도 된다.
  이 경우에도 ready source는 반드시 위 표의 pattern contract 와 일치해야 한다.
- active에서만 throughput/latency를 계산한다.
- `single`은 별도 settle/prime/idle-drain phase를 두지 않는다.
- 다음 size는 별도 프로세스로 다시 시작한다.
- monitor-ready 이후 필요한 protocol self-check는 단발성 검증 1회만
  허용하며, retry loop나 sleep 기반 보정은 금지한다.

### 2.1 Header 기반 집계 (필수)

active 구간 집계는 payload에 기록된 metric header를 기준으로만 수행한다.

- decode 실패 메시지: 집계 제외
- `magic`, `phase`, `msg_size` 검증 실패 메시지: 집계 제외
- 필요 시 `run_id` 불일치 메시지도 집계 제외한다.
- 유효 header 메시지만 throughput 카운트와 latency 샘플에 포함

즉, throughput과 latency는 동일한 유효 메시지 집합을 사용한다.

---

## 3. 유효성 판정 (single 전용)

> 상태 분류(success / unsupported / skip / fail), retry 금지, UNSUPPORTED 오용 금지
> 등 공통 실패 처리 정책은 [PERF_POLICY.md § 7](PERF_POLICY.md) 참조.

### 3.1 완료 판정

```text
expected = 요청된 전체 조합 수 - unsupported 수 - skip 수
actual   = 성공적으로 출력된 RESULT 라인 수
status   = (expected == actual) ? "complete" : "partial"
```

| status | 조건 |
|--------|------|
| complete | `expected == actual` |
| partial | `expected != actual` |

- single 정책에는 baseline 저장/비교 모드가 없다.
- partial이어도 결과 파일은 저장한다.

### 3.2 UNSUPPORTED 판정 (single 엔진 특성)

- single 실행 엔진은 stdout `UNSUPPORTED` 토큰만 인식한다.
- stderr `protocol not supported` 기반 자동 분류는 지원하지 않는다
  (multi 엔진에서만 지원).

---

## 4. 결과 저장 (single 전용)

> 파일명 형식(`perf_<lang>_<suite>_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt`),
> 저장 경로(`<suite>/report/`), 보존 정책(최대 100파일) 등 공통 규칙은
> [PERF_POLICY.md § 2.1–2.3, § 4.3](PERF_POLICY.md) 참조.

결과 파일에는 아래가 순서대로 기록된다.

1. `## Effective Options (start)` — 불릿 목록 형식 (lang, suite, runs, patterns, transports, msg_sizes, pin_cpu)
2. 패턴/트랜스포트별 실행 로그 및 테이블
3. `## Effective Options (result)` — 불릿 목록 형식
4. Completion (`status`, `expected_result_lines`, `actual_result_lines`)

- `Effective Options`에는 `lang`과 `suite` 항목이 반드시 포함되어야 한다.
- `tmp/`, `baseline/` 디렉터리는 single 정책에서 사용하지 않는다.
- single 엔진은 최대 파일 수를 100으로 하드코딩한다 (`PERF_RESULTS_MAX_FILES` 미참조).

---

## 5. 실행 방법

> 정책 준수 실행기 목록과 통합 실행 옵션은
> [PERF_POLICY.md § 3](PERF_POLICY.md) 참조.

### 5.1 CLI 옵션

| 옵션 | 설명 | 기본값 |
|------|------|--------|
| `--pattern NAME` | 패턴 목록 (쉼표 구분) | `ALL` |
| `--runs N` | 조합별 반복 횟수 | 1 |
| `--duration N` | active 구간 시간(초) | 5 |
| `--build-dir PATH` | 빌드 디렉터리 | 자동 탐색 |
| `--results-dir PATH` | 결과 루트 디렉터리 | `core/perf/results` |
| `--results-tag NAME` | 결과 파일명 태그 | 없음 |
| `--output PATH` | 콘솔 출력 tee 파일 | 없음 |
| `--pin-cpu` | CPU pinning | off |
| `--io-threads N` | context I/O threads | 환경/기본값 |
| `--msg-sizes LIST` | 메시지 크기 목록 | 정책 기본값 |
| `--transports LIST` | transport 목록 | 패턴 기본값 |
| `--hwm N` | 송수신 HWM 공통 fallback | 1000 |
| `--send-hwm N` | 송신 HWM 우선값 | `--hwm` |
| `--recv-hwm N` | 수신 HWM 우선값 | `--hwm` |

### 5.2 바이너리 직접 실행

```bash
<binary> <lib_name> <transport> <size>
# 예시
./core/build/linux-x64/bin/perf_pair current tcp 1024
```

---

## 6. Pattern & Transport Matrix

### 6.1 지원 패턴

- PAIR
- PUBSUB
- DEALER_DEALER
- DEALER_ROUTER
- ROUTER_ROUTER
- SPOT

> STREAM 계열(STREAM)은 single suite에서 테스트하지 않는다.

#### recv mode 지원 범위

| 패턴 | 허용 mode |
|------|-----------|
| PAIR | `recv` |
| PUBSUB | `recv` |
| DEALER_DEALER | `recv` |
| DEALER_ROUTER | `recv` |
| ROUTER_ROUTER | `recv` |
| SPOT | `recv` |

정책:

- single의 유일한 테스트 mode는 recv이다.

#### ready gate 기준

single의 send/recv 시작 가능 여부는 raw 패턴에서는 공식 monitor event,
SPOT 에서는 local probe barrier 로 판정한다. perf는 추가 precondition
(`FILTER_APPLIED`, quorum 완화)을 두지 않는다. 아래 contract 이후 메시징이
불가능하면 perf 우회가 아니라 core 버그로 보고 수정한다.

| 패턴 | 송신 시작 기준 | 수신 시작 기준 |
|------|----------------|----------------|
| PAIR | `CONNECTION_READY` | `CONNECTION_READY` |
| PUBSUB | `CONNECTION_READY` | `CONNECTION_READY` |
| DEALER_DEALER | `CONNECTION_READY` | `CONNECTION_READY` |
| DEALER_ROUTER | `CONNECTION_READY` | `CONNECTION_READY` |
| ROUTER_ROUTER | `CONNECTION_READY` | `CONNECTION_READY` |
| SPOT | local probe publish 후 first valid recv | local probe payload first valid recv |

- single policy 는 `event.value` 와 `snapshot.ready_count` gate 를 금지한다.
- single policy 는 delivery-ready event gate 도 사용하지 않는다.
- single SPOT 은 service monitor 를 사용하지 않는다.
- single SPOT 은 local pub/sub setup 완료 후 sender 가 metric header가 찍힌
  probe payload 를 publish 하고, recv 측이 첫 유효 payload 를 확인하면
  ready 를 닫는다.

#### 패턴 방향 분류

| 방향 | 패턴 | throughput 단위 |
|------|------|----------------|
| one-way (단방향) | PAIR, PUBSUB, DEALER_DEALER, DEALER_ROUTER, ROUTER_ROUTER, SPOT | `msg/s` |

> **구현 참고**: `core/perf/single/run_comparison.py`는 소켓 동작(echo/one-way)과 무관하게 모든 single 패턴을 **one-way 방향**, **Kmsg/s** 단위로 출력한다. bandwidth도 방향과 무관하게 `throughput × size / 1,000,000`으로 계산한다 (direction_factor를 적용하지 않는다).

### 6.2 표준 메시지 크기

`[64, 256, 1024, 65536, 131072, 262144]`

### 6.3 transport

| 패턴군 | transport |
|--------|-----------|
| PAIR / PUBSUB / DEALER / ROUTER | tcp, tls, ws, wss, inproc, ipc (Windows: ipc 제외) |
| SPOT | tcp, tls, ws, wss |

---

## 7. Environment Variables (single 전용)

> 공통 환경 변수(`PERF_DEBUG`, `PERF_IO_THREADS`, `PERF_MSG_SIZES`,
> `PERF_TRANSPORTS`, `PERF_TASKSET`, `PERF_FAIL_FAST`,
> `PERF_DISABLE_RESOURCE_METRICS`, `PERF_MAX_SOCKETS`)는
> [PERF_POLICY.md § 8](PERF_POLICY.md) 참조.

### 7.1 phase/timeout

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_SINGLE_DURATION_SECONDS` | active 구간 시간(초) | 5 |
| `PERF_SINGLE_TIMEOUT_SECONDS` | 프로세스 timeout(초) | `max(30, duration*6+15)` |

### 7.2 hwm/timeout

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_SINGLE_HWM` | 소켓 HWM 공통 fallback | 1000 |
| `PERF_SINGLE_SNDHWM` | 송신 HWM 우선값 | `PERF_SINGLE_HWM` |
| `PERF_SINGLE_RCVHWM` | 수신 HWM 우선값 | `PERF_SINGLE_HWM` |
| `PERF_SINGLE_SNDTIMEO_MS` | 송신 타임아웃(ms) | 200 |
| `PERF_SINGLE_RCVTIMEO_MS` | 수신 타임아웃(ms) | 200 |
| `PERF_SINGLE_PUBSUB_RCVTIMEO_MS` | PUBSUB 수신 타임아웃(ms) | `PERF_SINGLE_RCVTIMEO_MS` |
| `PERF_SINGLE_LATENCY_SAMPLE_CAP` | 레이턴시 샘플 최대 수 | 200000 |
| `PERF_SINGLE_PUBSUB_XPUB_NODROP` | PUBSUB의 `ZLINK_XPUB_NODROP` 기본값 | (바이너리별) |

- backpressure 검증은 `core/tests/integration`로 분리한다. one-way 통합 범위는
  `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `PUBSUB`, `SPOT` 이며,
  `STREAM`, echo, `PAIR` 은 제외한다.

---

## 8. 변경 이력

- **v2.0 (2026-04-07)**
  - 공통 정책을 [PERF_POLICY.md](PERF_POLICY.md)로 통합, 중복 제거
  - single 전용 내용만 유지
- **v1.9 (2026-03-21)**
  - 공통 원칙 및 바인딩 parity 기준 정렬
- **v1.6 (2026-03-03)**
  - baseline/mode/trend/gate 정책 제거
  - 결과 저장 구조를 `report/` 단일 경로로 정리
  - active 동시 측정 모델(throughput + latency) 명시
  - header decode/검증 성공 메시지만 집계하는 규칙 명문화
  - 드레인/재시도 미사용 정책 명시
