# zlink Single Performance Test Policy

> **적용 범위**: zlink 전체 (core + bindings) — single-client 벤치마크
> **Policy Version**: 1.9
> **Date**: 2026-03-21
> **Scope**: `perf/single` 성능 테스트 정책
>
> 본 정책은 `perf/single`의 C++ 벤치마크와 in-repo single perf 자산이 존재하는
> 바인딩(`bindings/cpp`, `bindings/dotnet`, `bindings/java`, `bindings/rust`,
> `bindings/go`, `bindings/node`, `bindings/python`)에 동일한 기준으로 적용한다.
> 단, 각 언어의 구현 완성도와 지원 패턴 범위는 다를 수 있으므로 실제 parity
> 수준은 언어별로 점검/정렬 대상이 된다.
>
> **상위 문서**: [PERF_POLICY.md](PERF_POLICY.md)
> **관련 문서**: [PERF_MULTI_TEST_POLICY.md](PERF_MULTI_TEST_POLICY.md)

---

## 1. 측정 원칙

| 항목 | 기준 |
|------|------|
| 측정 모델 | ready + active(duration) |
| throughput | `active 수신 건수 / active 시간(초)` |
| latency | active 구간 수신 payload header timestamp 기반 |
| 대표값 | runs > 1일 때 metric별 median |
| 저장 경로 | `perf/results/single/report/` 단일 |

- single은 **한 번의 active 구간에서 throughput + latency를 동시에** 측정한다.
- size 변경 시마다 별도 프로세스로 실행하여 케이스 간 메트릭 오염을 방지한다.
- `single`의 공식 lifecycle은 `ready -> active`다.
- 재시도 로직은 두지 않는다.
- 연결 준비/handshake는 pattern별 contract만 사용한다.
  - raw 패턴: low-cost ready event
  - SPOT: explicit local `READY/START` barrier
- ready bool/count를 복사하기 위한 별도 state struct, heap alloc, mutex/cv,
  callback wrapper 계층은 만들지 않는다.
- single perf의 ready gate는
  [`../guide/06-monitoring.ko.md`](../guide/06-monitoring.ko.md)의
  "메시징 시작 전 준비 확인" 절에 정의된 이벤트를 그대로 따른다.
- monitor-ready 이후 필요한 protocol self-check는 단발성 검증 1회만
  허용하며, retry loop나 sleep 기반 보정은 금지한다.
- start gate 구현에서 monitor snapshot polling은 금지한다.
- `setup_connected_pair()`, `wait_ready()`, service-ready wait helper는
  허용한다. 단:
  - raw 패턴 helper 는 low-cost event counting 만 수행해야 한다.
  - SPOT helper 는 explicit local `READY/START` barrier 만 수행해야 한다.
  - callback-state wrapper나 snapshot polling을 helper 뒤에 숨기는 방식은
    정책 위반이다.
- `single`에서 아래 단계/개념은 새로 만들지 않는다.
  - `preflight`
  - `prime`
  - `settle`
  - `stable`
  - `quiet`
  - `idle drain`
  - `expected_ready_count > 1`
- 위 항목이 이미 존재하지만 실제로는 ready 이벤트 하나 대기하거나 phase 종료를
  우회적으로 표현한 것뿐이면 삭제한다.

### 1.0.1 실행 계약 불변식

- `single`의 최소 측정 단위는 `pattern/transport/size/run` 이다.
- runner는 size마다 perf 바이너리를 **다시 실행**해야 한다.
- 하나의 perf 바이너리 프로세스가 여러 size를 내부 루프로 순회하면 정책 위반이다.
- perf 바이너리는 해당 size 케이스를 측정하고 `RESULT` line만 출력한다.
- size 반복 실행, runs 집계, markdown table 출력, 결과 파일 저장은 runner 책임이다.
- single 리팩토링은 위 책임 분리를 유지해야 하며, 변경 시 자동 검증(test)도
  함께 갱신해야 한다.

### 1.1 Single 핵심 정책

- 목적
  - 단일 소켓 경로에서 throughput, bandwidth, latency를 측정한다.
  - 같은 active 구간에서 동일 메시지 집합으로 latency도 함께 집계한다.
  - cpu/mem은 single 기본 perf surface와 RESULT 계약에 포함하지 않는다.
- single suite는 callback 모드만 지원한다.
- 수신 모델 (`--recv`)
  - **callback 모델** (`--recv callback`)만 허용한다.
  - `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`,
    `ROUTER_ROUTER`, `SPOT` 전부 callback only다.
  - callback 모드를 이유로 별도 callback 파일명이나 별도 public pattern 이름을
    두지 않는다.
  - recv: `zlink_recv_handler()` 등록 → 라이브러리가 I/O thread에서 callback
    dispatch. `zlink_recv()` / `zlink_msg_recv()` 동기 recv API는 측정 경로에
    사용하지 않는다.
  - callback hot path는 메시지에서 metric header와 timestamp 등 필요한 최소
    메타데이터만 추출해 bounded queue로 전달한다. `zlink_msg_t` handle,
    payload pointer, multipart parts 소유권을 callback 밖으로 넘기지 않는다.
  - callback dispatch thread는 phase별 receive count 증가와 metric event
    enqueue까지만 수행한다.
  - send: sender는 active 구간 동안 blocking send를 연속 수행한다.
  - throughput/latency 집계, phase window 판정, 결과 출력용 통계 계산은
    callback 안에서 직접 수행하지 않고 전용 worker가 queue를 drain하며
    처리한다. single callback 모델에서 app thread는 sender를 겸하지 않는다.
  - latency sample 계산, percentile sample 축적, processed-count 완료 대기는
    callback 밖 전용 worker가 맡는다.
  - 위 callback/worker 경계는 `PAIR`, `PUBSUB`, `DEALER_DEALER`,
    `DEALER_ROUTER`, `ROUTER_ROUTER`, `SPOT` 전 패턴에 동일하게 적용한다.
  - phase 종료 보장은 별도 bench phase가 아니라 내부 processed-count drain으로
    처리한다.
- poller
  - single 측정 경로에서는 사용하지 않는다.
- 공통
  - 실패 시 즉시 `fail` 처리한다.
  - retry는 없다.
  - single 전 패턴은 callback only다.
  - 지원하지 않는 single pattern에서 허용 범위 밖 mode를 주면 즉시 실패한다.
  - metric header decode, phase 판정, throughput/latency 집계 엔진은
    callback 경로 전체에서 공통으로 유지한다.
- 한 줄 요약
- `single = ready + active`

---

## 2. 바이너리 Phase 규칙

```text
[single phase]: [ready] -> [active(duration)]
```

| Phase | 방식 | 기본값 | 환경 변수 |
|-------|------|--------|-----------|
| ready | event-based | raw=`CONNECTION_READY`, SPOT=local `READY/START` barrier | `PERF_CONNECT_READY_TIMEOUT_MS` 계열 timeout |
| active | time-based | 5s | `PERF_SINGLE_DURATION_SECONDS` |

- `setup_connected_pair()`는 내부적으로 low-cost monitoring ready gate를
  캡슐화한 helper인 경우에만 허용된다. 별도/독자적인 start gate 규칙으로
  취급하지 않는다.
- pattern별 low-cost ready event는 single 측정의 공식 start gate다. benchmark
  시작 전 준비 판정은 monitoring event로 해결하고, perf 파일 안의 커스텀
  handshake loop, sleep, monitor snapshot polling으로 대체하지 않는다.
- 패턴 파일에서는 callback plumbing을 직접 노출하기보다 공통 helper를 통해
  `wait_*ready*()` 형태로 감싸도 된다. 이 경우에도 ready source는 반드시
  위 표의 pattern contract 와 일치해야 한다.
- active에서만 throughput/latency를 계산한다.
- `single`은 별도 settle/prime/idle-drain phase를 두지 않는다.
- 다음 size는 별도 프로세스로 다시 시작한다.

### 2.1 Header 기반 집계 (필수)

active 구간 집계는 payload에 기록된 metric header를 기준으로만 수행한다.

- decode 실패 메시지: 집계 제외
- `magic`, `phase`, `msg_size` 검증 실패 메시지: 집계 제외
- 필요 시 `run_id` 불일치 메시지도 집계 제외한다.
- 유효 header 메시지만 throughput 카운트와 latency 샘플에 포함

즉, throughput과 latency는 동일한 유효 메시지 집합을 사용한다.

---

## 3. 실행/집계 유효성

### 3.1 상태 분류

| 상태 | 조건 | 집계 |
|------|------|------|
| success | RESULT line 정상 출력 | 유효 결과 |
| unsupported | 정책 밖 pattern/transport 조합 | 제외, fail 아님 |
| skip | 환경 미충족 | 제외, fail 아님 (결과 테이블에서는 `fail`로 표시) |
| fail | timeout / no_data / non-zero exit | 무효 |

### 3.2 완료 판정

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

### 3.3 실패 처리

- 실패 조합 자동 재시도 금지
- `UNSUPPORTED` 오용 금지
  - 정책에 정의된 조합 실행 실패는 `fail`로 보고해야 한다.
  - single 실행 엔진은 stdout `UNSUPPORTED` 토큰만 인식한다. stderr `protocol not supported` 기반 자동 분류는 지원하지 않는다 (multi 엔진에서만 지원).

---

## 4. 결과 출력 형식

### 4.1 RESULT line

```text
RESULT,current,PAIR,tcp,1024,throughput,523401.23
RESULT,current,PAIR,tcp,1024,bandwidth,535.96
RESULT,current,PAIR,tcp,1024,latency,12.35
RESULT,current,PAIR,tcp,1024,latency_p95,18.10
RESULT,current,PAIR,tcp,1024,latency_p99,25.40
```

필수 metric (success 판정 기준):

- `throughput`
- `bandwidth`
- `latency`
- `latency_p95`
- `latency_p99`

출력 필수 metric:

- `throughput`
- `bandwidth`
- `latency`
- `latency_p95`
- `latency_p99`

완료 판정은 위 5개 Tier 1 metric RESULT line 기준으로 수행한다.

- cpu/mem 계열 metric은 single 기본 RESULT line에 포함하지 않는다.

### 4.2 사람이 읽는 테이블

runner는 RESULT line과 함께 pattern/transport별 markdown table을 stdout에 출력한다.
실행 중에는 size 행을 즉시 출력하고, runs > 1이면 run별 출력 후 median을 출력한다.

---

## 5. 결과 파일 저장

### 5.1 저장 구조

```text
perf/results/
└── single/
    └── report/
        ├── perf_linux_recv_YYYYMMDD_HHMMSS.txt
        ├── perf_linux_callback_YYYYMMDD_HHMMSS.txt
        ├── perf_linux_recv_YYYYMMDD_HHMMSS_<tag>.txt
        └── ...
```

- `tmp/`, `baseline/` 디렉터리는 single 정책에서 사용하지 않는다.
- 파일명 형식: `perf_<platform>_<recv_mode>_YYYYMMDD_HHMMSS[_<tag>].txt`
- `<recv_mode>`는 실제 실행에 사용된 `--recv` 값이며 `recv` 또는 `callback`이다.
- `--results-tag` 지정 시 `<tag>`가 파일명에 추가된다.

### 5.2 저장 내용

결과 파일에는 아래가 순서대로 기록된다.

1. `## Effective Options (start)` — 불릿 목록 형식
2. 패턴/트랜스포트별 실행 로그 및 테이블
3. `## Effective Options (result)` — 불릿 목록 형식
4. Completion (`status`, `expected_result_lines`, `actual_result_lines`)

- `Effective Options`에는 `recv_mode` 항목이 반드시 포함되어야 하며, 실제 실행에
  사용된 `--recv` 값(`recv` 또는 `callback`)을 기록해야 한다.

### 5.3 보존 정책

| 디렉터리 | 최대 파일 수 | 초과 시 처리 |
|-----------|-------------|-------------|
| `report/` | 100 | 파일명 사전순 기준 오래된 파일 삭제 |

---

## 6. 실행 방법

### 6.1 정책 준수 실행기

- Linux: `core/perf/run_benchmarks.sh`
- Windows: `core/perf/run_benchmarks.ps1`

single suite 공식 결과는 위 실행기로만 생성한다.
직접 바이너리 실행은 디버깅 용도로만 사용한다.

### 6.2 CLI 옵션 (공통 의미)

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
| `--recv MODE` | recv 모델 선택. single은 `callback`만 허용 | `callback` |
| `--hwm N` | 송수신 HWM 공통 fallback | 1000 |
| `--send-hwm N` | 송신 HWM 우선값 | `--hwm` |
| `--recv-hwm N` | 수신 HWM 우선값 | `--hwm` |

### 6.3 바이너리 직접 실행

```bash
<binary> <lib_name> <transport> <size>
# 예시
./core/build/linux-x64/bin/perf_pair current tcp 1024
```

---

## 7. Pattern & Transport Matrix

### 7.1 지원 패턴

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
| PAIR | `callback` |
| PUBSUB | `callback` |
| DEALER_DEALER | `callback` |
| DEALER_ROUTER | `callback` |
| ROUTER_ROUTER | `callback` |
| SPOT | `callback` |

정책:

- single의 유일한 테스트 mode는 callback이다.
- callback 모드를 이유로 별도 callback 파일명이나 별도 public pattern 이름을
  정책에 추가하지 않는다.

#### ready gate 기준

single의 send/recv 시작 가능 여부는 아래 공식 monitor event만으로 판정한다.
perf는 추가 precondition(`FILTER_APPLIED`, custom handshake, quorum 완화)을
두지 않는다. 아래 이벤트 이후 메시징이 불가능하면 perf 우회가 아니라 core
버그로 보고 수정한다.

| 패턴 | 송신 시작 기준 | 수신 시작 기준 |
|------|----------------|----------------|
| PAIR | `CONNECTION_READY` | `CONNECTION_READY` |
| PUBSUB | `CONNECTION_READY` | `CONNECTION_READY` |
| DEALER_DEALER | `CONNECTION_READY` | `CONNECTION_READY` |
| DEALER_ROUTER | `CONNECTION_READY` | `CONNECTION_READY` |
| ROUTER_ROUTER | `CONNECTION_READY` | `CONNECTION_READY` |
| SPOT | explicit local `READY/START` barrier | explicit local `READY/START` barrier |

- single policy 는 `event.value` 와 `snapshot.ready_count` gate 를 금지한다.
- single policy 는 delivery-ready event gate 도 사용하지 않는다.
- single SPOT 은 service monitor 를 사용하지 않는다.
- single SPOT 은 local pub/sub setup 완료 후 explicit local `READY/START`
  barrier 로 시작한다.

#### 패턴 방향 분류

| 방향 | 패턴 | throughput 단위 |
|------|------|----------------|
| one-way (단방향) | PAIR, PUBSUB, DEALER_DEALER, DEALER_ROUTER, ROUTER_ROUTER, SPOT | `msg/s` |

> **구현 참고**: `core/perf/single/run_comparison.py`는 소켓 동작(echo/one-way)과 무관하게 모든 single 패턴을 **one-way 방향**, **Kmsg/s** 단위로 출력한다. bandwidth도 방향과 무관하게 `throughput × size / 1,000,000`으로 계산한다 (direction_factor를 적용하지 않는다).

### 7.2 표준 메시지 크기

`[64, 256, 1024, 65536, 131072, 262144]`

### 7.3 transport

| 패턴군 | transport |
|--------|-----------|
| PAIR / PUBSUB / DEALER / ROUTER | tcp, tls, ws, wss, inproc, ipc (Windows: ipc 제외) |
| SPOT | tcp, tls, ws, wss |

---

## 8. Environment Variables

### 8.1 공통

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_DEBUG` | 디버그 로그 | unset |
| `PERF_IO_THREADS` | context I/O threads | 2 |
| `PERF_MSG_SIZES` | size 목록 override (러너가 size별 케이스로 분할 실행) | 정책 기본값 |
| `PERF_TRANSPORTS` | transport 목록 override | 패턴 기본값 |
| `PERF_TASKSET` | CPU pinning 활성화 (`1`) | 0 |
| `PERF_FAIL_FAST` | 실패 시 즉시 중단 (`1`) | 0 |
| `PERF_DISABLE_RESOURCE_METRICS` | 리소스 메트릭(CPU/메모리) 수집 비활성화 (`1`로 활성화) | 0 |

### 8.2 phase/timeout

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_SINGLE_DURATION_SECONDS` | active 구간 시간(초) | 5 |
| `PERF_SINGLE_TIMEOUT_SECONDS` | 프로세스 timeout(초) | `max(30, duration*6+15)` |

### 8.3 hwm/timeout

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
| `PERF_MAX_SOCKETS` | context max sockets | auto |

- backpressure 검증은 `core/tests/integration`로 분리한다. one-way 통합 범위는
  `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `PUBSUB`, `SPOT` 이며,
  `STREAM`, echo, `PAIR` 은 제외한다.

---

## 9. 구현 제약

### 9.1 Public API 전용 / Retry·우회 금지

- `core/perf/single`은 `doc/guide` 및 `doc/api` 문서에 기술된 public C API만
  사용한다. 내부 헤더나 내부 함수를 직접 호출하지 않는다.
- `bindings/<lang>/perf/single`은 해당 언어 binding의 public API만 사용한다.
  binding 내부/private API, 내부 구현 클래스, native 내부 helper를 직접 호출하지
  않는다.
- public API 동작에 문제가 있으면 bench 코드에서 우회하지 않고 버그로
  레포팅한다. core 또는 binding public API를 수정한 뒤 bench 작업을 계속한다.
- core와 bindings는 single 측정 anchor를 동일 의미로 유지해야 한다.
  - ready 만족 판정
  - active 시작/종료
  - metric header decode 유효 판정
  - throughput count 증가
  - latency sample 채취
  - RESULT line 출력
- core와 bindings는 아래 single 비교 가능성 조건도 함께 만족해야 한다.
  - 같은 pattern/transport 의미를 측정한다.
  - 같은 metric header / wire protocol contract를 사용한다.
  - 같은 Tier 1 5개 metric과 같은 fail/skip/partial 의미를 사용한다.
  - hot path가 실제 binding public API를 통과한다.
- 단, 위 anchor와 결과 의미가 같다면 구현 스타일은 언어별 runtime/idiom에 맞게
  다르게 작성할 수 있다.
- 실패 조합 자동 재시도 로직 금지
- core 문제를 벤치마크 코드에서 우회하지 않는다

### 9.2 Stub 파일 금지

`#include` 한 줄로 구현 전체를 위임하는 stub 소스 금지.
각 벤치마크 소스는 해당 패턴 동작(소켓 생성, bind/connect, send/recv 루프,
phase 제어)을 파일 단독으로 이해 가능해야 한다.

### 9.3 hot path

측정 경로(hot path)에서 불필요한 lock/동적할당/과도한 로깅을 피한다.

---

## 10. 변경 이력

- **v1.6 (2026-03-03)**
  - baseline/mode/trend/gate 정책 제거
  - 결과 저장 구조를 `report/` 단일 경로로 정리
  - active 동시 측정 모델(throughput + latency) 명시
  - header decode/검증 성공 메시지만 집계하는 규칙 명문화
  - 드레인/재시도 미사용 정책 명시
