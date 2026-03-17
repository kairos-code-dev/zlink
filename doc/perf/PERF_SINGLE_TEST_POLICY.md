# zlink Single Performance Test Policy

> **적용 범위**: zlink 전체 (core + bindings) — single-client 벤치마크
> **Policy Version**: 1.6
> **Date**: 2026-03-03
> **Scope**: `perf/single` 성능 테스트 정책
>
> 본 정책은 `perf/single`의 C++ 벤치마크뿐 아니라 모든 바인딩 라이브러리
> (`bindings/cpp`, `bindings/dotnet`, `bindings/java`, `bindings/node`,
> `bindings/python`)의 single 성능 테스트에도 동일하게 적용된다.
>
> **상위 문서**: [PERF_POLICY.md](PERF_POLICY.md)
> **관련 문서**: [PERF_MULTI_TEST_POLICY.md](PERF_MULTI_TEST_POLICY.md)

---

## 1. 측정 원칙

| 항목 | 기준 |
|------|------|
| 측정 모델 | warmup(count) + active(duration) |
| throughput | `active 수신 건수 / active 시간(초)` |
| latency | active 구간 수신 payload header timestamp 기반 |
| 대표값 | runs > 1일 때 metric별 median |
| 저장 경로 | `perf/results/single/report/` 단일 |

- single은 **한 번의 active 구간에서 throughput + latency를 동시에** 측정한다.
- size 변경 시마다 별도 프로세스로 실행하여 케이스 간 메트릭 오염을 방지한다.
- 명시적 drain phase는 두지 않지만, active 종료 후 receiver는 짧은 idle drain으로 in-flight 메시지를 정리할 수 있다.
- 재시도 로직은 두지 않는다.
- 연결 준비/handshake는 monitor 기반 readiness를 사용한다.
- monitor-ready 이후 필요한 protocol self-check는 단발성 검증 1회만
  허용하며, retry loop나 sleep 기반 보정은 금지한다.

### 1.1 Single 핵심 정책

- 목적
  - 단일 소켓 경로에서 **자연 backpressure를 유지한 채 가능한 최대 throughput**을 측정한다.
  - 같은 active 구간에서 동일 메시지 집합으로 latency도 함께 집계한다.
- send
  - sender는 active 구간 동안 blocking send를 연속 수행한다.
  - 실패 시 즉시 `fail` 처리한다.
  - retry는 없다.
- recv
  - receiver는 callback-only recv 경로를 사용한다.
  - `zlink_recv()` / `zlink_msg_recv()` 같은 동기 recv API는 active/warmup
    측정 경로에
    사용하지 않는다.
  - active 종료 후에는 callback dispatch 기준의 bounded idle drain으로
    잔여 in-flight를 정리할 수 있다.
- poller
  - single의 기본 메커니즘이 아니다.
  - `ROUTER_ROUTER_POLL`도 이름만 유지하고 동일 정책을 적용한다.
- 한 줄 요약
  - `single = active sender + concurrent receiver + nonblocking drain`

---

## 2. 바이너리 Phase 규칙

```text
[single phase]: [warmup] -> [settle(100ms, 일부 패턴)] -> [active(duration)] -> [idle drain]
```

| Phase | 방식 | 기본값 | 환경 변수 |
|-------|------|--------|-----------|
| warmup | count-based | 패턴별 기본값 (표준: 1000, GATEWAY/SPOT: 200, SPOT msg_size≥65536: 20) | `PERF_WARMUP_COUNT` |
| settle | time-based | 100ms | — (코드 상수 `SETTLE_TIME_MS`) |
| active | time-based | 5s | `PERF_SINGLE_DURATION_SECONDS` |
| idle drain | idle-based | recv timeout (기본 200ms) 동안 무수신 시 종료 | `PERF_SINGLE_RCVTIMEO_MS` |

- warmup 데이터는 최종 집계에서 제외한다.
- settle은 소켓 설정 완료 후 안정화 대기이며, 환경 변수로 변경할 수 없다. **GATEWAY, SPOT 등 서비스 패턴에만 적용된다.** 소켓 패턴(PAIR, PUBSUB, DEALER_*, ROUTER_*)은 `setup_connected_pair()` 내부에서 연결 안정화를 처리하므로 별도 settle을 호출하지 않는다.
- `setup_connected_pair()` 및 service monitor readiness는 single 측정의
  공식 start gate다. benchmark 시작 전 준비 판정은 monitoring을 통해
  해결하고, perf 파일 안의 커스텀 handshake loop로 대체하지 않는다.
- active에서만 throughput/latency를 계산한다.
- idle drain은 active 종료 전에 이미 송신된 in-flight 메시지를 정리하기 위한 receiver 측 정리 단계다.
- 다음 size는 별도 프로세스로 다시 시작한다.

### 2.1 Header 기반 집계 (필수)

active 구간 집계는 payload에 기록된 metric header를 기준으로만 수행한다.

- decode 실패 메시지: 집계 제외
- `magic`, `phase` 검증 실패 메시지: 집계 제외 (구현은 `magic` + `phase` 두 필드만 검증한다. `is_expected()` 유틸리티에 `run_id`/`msg_size` 검증이 정의되어 있으나, 실제 벤치마크 코드에서는 사용하지 않는다)
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
RESULT,current,PAIR,tcp,1024,cpu_pct,48.20
RESULT,current,PAIR,tcp,1024,mem_mb,12.30
RESULT,current,PAIR,tcp,1024,snd_pending_max,0
RESULT,current,PAIR,tcp,1024,rcv_pending_max,0
RESULT,current,PAIR,tcp,1024,rcv_pending_end,0
```

필수 metric (success 판정 기준):

- `throughput`
- `bandwidth`
- `latency`

출력 필수 metric (success 시 항상 출력, 누락 시 mean latency로 fallback 보정):

- `latency_p95`
- `latency_p99`

> **구현 참고**: `run_comparison.py`의 success 판정은 `throughput`, `bandwidth`, `latency` 3개만 검사한다. `latency_p95`/`latency_p99`는 누락 시 mean latency 값으로 자동 보정(`resolve_latency_triplet`)하여 출력하므로, 이들의 부재가 success 판정을 방해하지 않는다.

정보성 metric(없어도 complete 판정에 영향 없음):

- `cpu_pct`, `mem_mb`
- `snd_pending_max`, `rcv_pending_max`, `rcv_pending_end`

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
        ├── perf_linux_YYYYMMDD_HHMMSS.txt
        ├── perf_linux_YYYYMMDD_HHMMSS_<tag>.txt
        └── ...
```

- `tmp/`, `baseline/` 디렉터리는 single 정책에서 사용하지 않는다.
- 파일명 형식: `perf_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt`
- `--results-tag` 지정 시 `<tag>`가 파일명에 추가된다.

### 5.2 저장 내용

결과 파일에는 아래가 순서대로 기록된다.

1. `## Effective Options (start)` — 불릿 목록 형식
2. 패턴/트랜스포트별 실행 로그 및 테이블
3. `## Effective Options (result)` — 불릿 목록 형식
4. Completion (`status`, `expected_result_lines`, `actual_result_lines`)

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
- ROUTER_ROUTER_POLL
- GATEWAY
- SPOT

> STREAM 계열(STREAM, STREAM_CALLBACK, STREAM_LEN32BE)은 single suite에서 테스트하지 않는다.

#### 패턴 방향 분류

| 방향 | 패턴 | throughput 단위 |
|------|------|----------------|
| one-way (단방향) | PAIR, PUBSUB, DEALER_DEALER, DEALER_ROUTER, ROUTER_ROUTER, GATEWAY, SPOT | `msg/s` |

> **구현 참고**: `run_comparison.py`는 소켓 동작(echo/one-way)과 무관하게 모든 single 패턴을 **one-way 방향**, **Kmsg/s** 단위로 출력한다. bandwidth도 방향과 무관하게 `throughput × size / 1,000,000`으로 계산한다 (direction_factor를 적용하지 않는다).

### 7.2 표준 메시지 크기

`[64, 256, 1024, 65536, 131072, 262144]`

### 7.3 transport

| 패턴군 | transport |
|--------|-----------|
| PAIR / PUBSUB / DEALER / ROUTER | tcp, tls, ws, wss, inproc, ipc (Windows: ipc 제외) |
| GATEWAY / SPOT | tcp, tls, ws, wss |

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
| `PERF_WARMUP_COUNT` | warmup 메시지 개수 | 패턴별 기본값 (표준: 1000, GATEWAY/SPOT: 200) |
| `PERF_SINGLE_DURATION_SECONDS` | active 구간 시간(초) | 5 |
| `PERF_SINGLE_TIMEOUT_SECONDS` | 프로세스 timeout(초) | `max(30, duration*6+15)` |

### 8.3 queue/hwm/timeout

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `PERF_SINGLE_HWM` | 소켓 HWM 공통 fallback | 1000 |
| `PERF_SINGLE_SNDHWM` | 송신 HWM 우선값 | `PERF_SINGLE_HWM` |
| `PERF_SINGLE_RCVHWM` | 수신 HWM 우선값 | `PERF_SINGLE_HWM` |
| `PERF_SINGLE_SNDTIMEO_MS` | 송신 타임아웃(ms) | 200 |
| `PERF_SINGLE_RCVTIMEO_MS` | 수신 타임아웃(ms) | 200 |
| `PERF_SINGLE_PUBSUB_RCVTIMEO_MS` | PUBSUB 수신 타임아웃(ms) | `PERF_SINGLE_RCVTIMEO_MS` |
| `PERF_SINGLE_QUEUE_SAMPLE_MS` | queue pending 샘플링 주기(ms) | 100 |
| `PERF_SINGLE_QUEUE_SAMPLE_EVERY_MSGS` | queue pending 샘플링 메시지 간격 | 64 |
| `PERF_SINGLE_LATENCY_SAMPLE_CAP` | 레이턴시 샘플 최대 수 | 200000 |
| `PERF_SINGLE_PUBSUB_XPUB_NODROP` | PUBSUB의 `ZLINK_XPUB_NODROP` 기본값 | (바이너리별) |
| `PERF_MAX_SOCKETS` | context max sockets | auto |

---

## 9. 구현 제약

### 9.1 Retry/우회 금지

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
