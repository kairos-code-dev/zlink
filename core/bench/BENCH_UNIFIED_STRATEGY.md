# Unified Benchmark Strategy

> **Reference Standard**: `benchwithzlink/multi`
> **Date**: 2026-02-22
> **Scope**: benchwithzlink/multi, benchwithroutercompare, benchwithstreamcompare, benchwithzmq/multi
> **범위 외**: single 클라이언트 테스트 (benchwithzmq/single, benchwithzlink/single 등)는 이 문서의 범위가 아님

---

## 1. Overview

이 문서는 `core/bench/` 아래 4개 벤치마크 스위트의 테스트 전략, 메트릭 수집 규칙,
메트릭 항목을 `benchwithzlink/multi` 기준으로 통합 정리한다.

### 1.1 현재 상태 요약

| 항목 | benchwithzlink/multi | benchwithroutercompare | benchwithstreamcompare | benchwithzmq |
|------|---------------------|----------------------|----------------------|-------------|
| **비교 대상** | baseline vs current (zlink) | zlink vs libzmq vs gRPC | zlink vs asio vs cppserver 등 | zlink vs libzmq |
| **소켓 패턴** | DEALER, ROUTER, PUBSUB, STREAM, GATEWAY, SPOT | ROUTER echo | STREAM (len32be) | DEALER, ROUTER, PUBSUB, STREAM |
| **트랜스포트** | tcp, tls, ws, wss | tcp only | tcp only | tcp |
| **결과 포맷** | `RESULT,lib,pattern,transport,size,metric,value` | 동일 | 독자 CSV/JSON | 동일 |
| **통계 집계** | median (N runs) | median (N runs) | median (N runs) | median (N runs) |
| **리소스 수집** | 없음 | CPU%, RSS (sampling) | CPU%, RSS, system mem | 없음 |
| **에러 추적** | fatal_error flag | pass/fail | send_err/recv_err/timeout/size_mismatch | pass/fail |
| **안정성 분석** | 없음 | 없음 | CV%, confidence, gate (**삭제 예정**) | 없음 |

### 1.2 통합 목표

1. **결과 출력 포맷**: 모든 벤치마크가 동일한 `RESULT,` 라인 포맷 사용
2. **메트릭 항목**: 공통 필수 메트릭 + 선택 확장 메트릭 정의
3. **테스트 페이즈**: warmup → settle → duration 3단계 통일
4. **통계 집계**: median 기반 단일 방식 통일 (CV%/gate/confidence 안정성 분석은 사용하지 않음)
5. **환경 변수**: 네이밍 컨벤션 통일

---

## 2. 통합 결과 출력 포맷 (RESULT Line Protocol)

모든 벤치마크 바이너리는 아래 포맷으로 stdout에 결과를 출력해야 한다.

### 2.1 필수 메트릭 라인

```
RESULT,<lib>,<pattern>,<transport>,<size>,throughput,<value>
RESULT,<lib>,<pattern>,<transport>,<size>,latency,<value>
```

| 필드 | 설명 | 예시 |
|------|------|------|
| `lib` | 라이브러리/스택 이름 | `current`, `baseline`, `libzmq`, `grpc` |
| `pattern` | 소켓 패턴 | `DEALER_DEALER`, `ROUTER_ECHO`, `STREAM`, `MULTI_STREAM` |
| `transport` | 전송 프로토콜 | `tcp`, `tls`, `ws`, `wss`, `inproc`, `ipc` |
| `size` | 메시지 크기 (bytes) | `64`, `1024`, `65536` |
| `throughput` | 처리량 (msgs/sec) | `150000.00` |
| `latency` | 레이턴시 (microseconds, roundtrip avg) | `45.23` |

### 2.2 선택 확장 메트릭 라인

> **⚠ 현행 파서 호환성 주의**
>
> 현재 `common/multi_e2e_metrics.py`의 `parse_results()`와 `run_comparison.py`의 `parse_result_line()`은
> `throughput`과 `latency` 두 메트릭만 파싱하며, 그 외 메트릭 라인은 **무시(skip)** 된다.
> Tier 2/3 메트릭 라인을 바이너리에서 출력하더라도, 비교 테이블에 반영하려면
> 파서 확장이 선행되어야 한다 (구현 체크리스트 Phase 2 참조).

리소스 모니터링이 필요한 벤치마크(routercompare, streamcompare)는 추가 라인을 출력할 수 있다.

```
RESULT,<lib>,<pattern>,<transport>,<size>,server_cpu_pct,<value>
RESULT,<lib>,<pattern>,<transport>,<size>,server_rss_mb,<value>
RESULT,<lib>,<pattern>,<transport>,<size>,client_cpu_pct,<value>
RESULT,<lib>,<pattern>,<transport>,<size>,client_rss_mb,<value>
RESULT,<lib>,<pattern>,<transport>,<size>,system_cpu_pct,<value>
RESULT,<lib>,<pattern>,<transport>,<size>,system_mem_pct,<value>
RESULT,<lib>,<pattern>,<transport>,<size>,latency_p95,<value>
RESULT,<lib>,<pattern>,<transport>,<size>,latency_p99,<value>
RESULT,<lib>,<pattern>,<transport>,<size>,throughput_bps,<value>
```

### 2.3 준비 상태 라인 (선택)

멀티 클라이언트 벤치마크에서 연결 준비 시간을 추적할 때 사용한다.
조건부 출력이며, `BENCH_STREAM_SHOW_PREP=1` 또는 `BENCH_DEBUG=1` 설정 시에만 표시된다.

```
PREP,<lib>,<pattern>,<transport>,<size>,connect_ms,<value>,ready_ms,<value>
```

---

## 3. 통합 테스트 페이즈

모든 벤치마크는 다음 3단계를 따른다.

```
[warmup] → [settle] → [duration]
```

### 3.1 페이즈 정의

| 페이즈 | 목적 | 기본값 | 환경 변수 | 구현 레벨 |
|--------|------|--------|-----------|-----------|
| **warmup** | 측정 전 워밍업 (send/recv 순환) | 3s | `BENCH_MULTI_WARMUP_SECONDS` | C++ (`bench_*_multi_pubsub.cpp`) + 쉘 타임아웃 계산 |
| **settle** | 연결 후 버퍼 안정화 대기 | 500ms | `BENCH_MULTI_SETTLE_MS` | C++ (`bench_common_multi.hpp`) |
| **duration** | 실제 측정 구간 | 5s | `BENCH_MULTI_DURATION_SECONDS` | C++ (`bench_common_multi.hpp`) |
| ~~**drain**~~ | *(폐지)* 잔여 in-flight 메시지 소진 | ~~300ms~~ | `BENCH_MULTI_DRAIN_MS` | *(일부 pubsub/stream에만 잔존, 통합 후 제거 예정)* |

> **참고 — warmup 페이즈의 구현 위치**
>
> `bench_common_multi.hpp`의 `multi_bench_settings_t`에 `warmup_seconds` 필드가 존재하며,
> pubsub 벤치마크(`bench_*_multi_pubsub.cpp`)에서 측정 전 send/recv 순환을 통해 **실제 워밍업을 수행**한다.
> C++ 바이너리 레벨의 전체 측정 페이즈는 `warmup → settle → duration` 3단계이다 (drain은 사용하지 않는다).
> 추가로, 쉘 스크립트(`run_benchmarks_multi.sh`)와
> Python 비교 러너(`run_comparison.py`)에서 **프로세스 타임아웃 계산**에도 포함된다:
>
> ```
> timeout = warmup_seconds + duration_seconds + extra_margin
> ```

### 3.2 페이즈 적용 규칙

- **다중 클라이언트 패턴** (MULTI_*):
  C++ 바이너리 내부에서 warmup → settle → duration 3단계를 실행한다.
  drain 페이즈는 사용하지 않는다 (측정 종료 후 소켓을 즉시 닫는다).
  duration은 시간 기반 (seconds), settle은 밀리초 기반.
- **비교 벤치마크** (routercompare, streamcompare, zmq/multi):
  대상 스위트의 페이즈 전략을 따르되, 동일 조건에서 모든 스택을 실행

### 3.3 측정 구간 규칙

- throughput = duration 구간의 `recv_count / duration_seconds`
- latency = duration 구간의 roundtrip 평균 (`elapsed_us / (roundtrip_count * 2)`)
- warmup 구간의 데이터는 최종 메트릭에서 **제외**한다

---

## 4. 통합 메트릭 항목

### 4.1 Tier 1: 필수 메트릭 (모든 벤치마크)

| 메트릭 | 단위 | 설명 | 계산 방식 |
|--------|------|------|-----------|
| `throughput` | msgs/sec | 메시지 처리량 | duration 구간 recv_count / duration_seconds |
| `latency` | us (microseconds) | 평균 라운드트립 지연 | duration 구간 total_elapsed_us / (count * 2) |

### 4.2 Tier 2: 권장 메트릭 (다중 클라이언트 / 서버-클라이언트 분리 벤치마크)

| 메트릭 | 단위 | 설명 |
|--------|------|------|
| `latency_p95` | us | 95th percentile 레이턴시 |
| `latency_p99` | us | 99th percentile 레이턴시 |
| `throughput_bps` | bytes/sec | 바이트 단위 처리량 (`throughput * msg_size`) |
| `connect_ms` | ms | 전체 클라이언트 연결 완료 시간 |
| `ready_ms` | ms | 연결 후 준비 완료 대기 시간 |

### 4.3 Tier 3: 선택 메트릭 (리소스 모니터링 활성 시)

| 메트릭 | 단위 | 설명 | 수집 방식 |
|--------|------|------|-----------|
| `server_cpu_pct` | % | 서버 프로세스 평균 CPU 사용률 | /proc/[pid]/stat 샘플링 (250ms 간격) |
| `server_rss_mb` | MB | 서버 프로세스 peak RSS | /proc/[pid]/status VmRSS 샘플링 |
| `client_cpu_pct` | % | 클라이언트 프로세스 평균 CPU 사용률 | 동일 (CPU: /proc/[pid]/stat) |
| `client_rss_mb` | MB | 클라이언트 프로세스 peak RSS | 동일 (RSS: /proc/[pid]/status VmRSS) |
| `system_cpu_pct` | % | 시스템 전체 평균 CPU 사용률 | /proc/stat 기반 |
| `system_mem_pct` | % | 시스템 전체 메모리 사용률 | /proc/meminfo 기반 |

---

## 5. 통계 집계 규칙

### 5.1 기본 집계: Median

모든 벤치마크에서 N회 반복 실행 시, 각 메트릭의 **median** 값을 대표값으로 사용한다.

```python
final_value = statistics.median(run_values)
```

- 단일 실행 시 해당 값을 그대로 사용

#### 반복 횟수 기본값 (As-is → To-be)

| 스위트 | As-is (현행) | To-be (목표) | 비고 |
|--------|-------------|-------------|------|
| benchwithzlink/multi | **1** (엔트리) / 3 (py 직접) | 3 | `run_benchmarks.sh` `RUNS=1`, `run_comparison.py` `DEFAULT_NUM_RUNS=3` |
| benchwithroutercompare | 5 | 3 | `run_benchmarks.sh` `RUNS=5` |
| benchwithstreamcompare | 1 | 3 | `run_benchmarks.sh` `RUNS=1` |
| benchwithzmq/multi | 1 | 3 | `run_benchmarks.sh` `RUNS=1` |

> To-be: 모든 스위트에서 `--runs 3`을 기본값으로 통일한다.

### 5.2 안정성 분석 - 삭제 대상

> **결정**: CV%, gate_pass, confidence level 등 안정성 분석 메커니즘은 **사용하지 않는다**.
> benchwithstreamcompare에 존재하는 아래 항목들은 통합 과정에서 **제거**한다.
>
> - CV% (Coefficient of Variation) 계산 및 STABLE/UNSTABLE 판정
> - gate_pass (pass_rate + peak + cv 복합 조건)
> - confidence level 라벨 (STABLE_CHECKED / UNSTABLE)
> - 랭킹 테이블의 Gate 컬럼
>
> **이유**: median 기반 집계만으로 충분하며, gate/confidence 로직이
> 벤치마크 간 불필요한 복잡도를 추가한다. 결과의 신뢰성은 반복 횟수(`--runs`)로 확보한다.

### 5.3 비교 Diff% 계산

라이브러리 간 비교 시 diff%는 다음과 같이 계산한다.

```
throughput_diff = ((current - base) / base) * 100.0    # 양수 = current가 빠름
latency_diff    = ((base - current) / base) * 100.0    # 양수 = current가 빠름
```

- base ≤ 0인 경우 diff = `"N/A"` 로 표시 (수치 계산 불가)
- `+` 접두사 = current가 우세, `-` 접두사 = base가 우세

---

## 6. 통합 환경 변수 네이밍

### 6.1 공통 (모든 벤치마크)

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `BENCH_DEBUG` | 디버그 로그 활성화 | (unset) |
| `BENCH_IO_THREADS` | ZMQ/ZLink context I/O 스레드 수 | 0 (auto) |
| `BENCH_MSG_SIZES` | 테스트 메시지 크기 목록 (comma-separated) | `64,256,1024,65536,131072,262144` |
| `BENCH_MSG_COUNT` | 메시지 수 override (benchwithzlink, benchwithzmq만 해당²) | auto (크기에 따라 결정) |
| `BENCH_TRANSPORTS` | 테스트 트랜스포트 목록 | 패턴별 기본값 |
| `BENCH_TASKSET` | Linux CPU pinning 활성화 (`1`) | 0 |
| `BENCH_FAIL_FAST` | 첫 실패 시 즉시 중단 | 0 |

> ² routercompare와 streamcompare는 시간 기반(`BENCH_MULTI_DURATION_SECONDS`)으로 동작하며 `BENCH_MSG_COUNT`를 사용하지 않는다.

### 6.2 다중 클라이언트 전용 (`BENCH_MULTI_*`)

아래 테이블의 **As-is** 열은 현행 코드에서의 기본값, **To-be** 열은 통합 후 목표 기본값이다.
As-is = To-be인 항목은 To-be에 `=`로 표시한다.

| 변수 | 설명 | As-is (현행) | To-be (목표) |
|------|------|-------------|-------------|
| `BENCH_MULTI_CLIENTS` | 클라이언트 소켓 수 | 100 (STREAM: 10000) | = |
| `BENCH_MULTI_INFLIGHT` | 클라이언트당 in-flight 메시지 수 | 30 | = |
| `BENCH_MULTI_HWM` | 소켓 HWM (SNDHWM/RCVHWM) | 100000 | = |
| `BENCH_MULTI_WARMUP_SECONDS` | 측정 전 워밍업 시간 (초) | 3 | = |
| `BENCH_MULTI_DURATION_SECONDS` | 측정 시간 (초) | **zlink/multi: 10, zmq/multi: 5** | **5** |
| `BENCH_MULTI_SETTLE_MS` | settle 대기 (ms) | 500 | = |
| `BENCH_MULTI_DRAIN_MS` | *(deprecated)* drain 대기 (ms) | 300 | **0 (제거)** |
| `BENCH_MULTI_SEND_WORKERS` | 송신 워커 스레드 수 | **zlink/multi: auto¹, zmq/multi: 2** | auto¹ |
| `BENCH_MULTI_SEND_BACKOFF_US` | 송신 블록 시 backoff (us) | 20 | = |
| `BENCH_MULTI_RECV_BATCH` | 수신 배치 크기 | 64 | = |
| `BENCH_MULTI_CONNECT_CONCURRENCY` | 동시 연결 수 | **128** | **1024** |
| `BENCH_MULTI_CONNECT_READY_TIMEOUT_MS` | 연결 준비 타임아웃 (ms) | 5000 | = |
| `BENCH_MULTI_BLOCKING_SEND` | 블로킹 전송 모드 | 0 | = |
| `BENCH_MULTI_SNDTIMEO_MS` | 송신 타임아웃 (ms) | 5000 | = |
| `BENCH_MULTI_RCVTIMEO_MS` | 수신 타임아웃 (ms) | 5000 | = |
| `BENCH_MULTI_MONITOR_HWM` | 모니터 소켓 HWM | 200000 | = |
| `BENCH_SERVER_RECV_THREADS` | 서버 수신 스레드 수 | 1 | = |

> ¹ auto: `clients >= 10000 ? 4 : 1` (bench_common_multi.hpp 기준)

#### benchwithzmq/multi 현행 차이 상세

| 항목 | benchwithzlink/multi (기준) | benchwithzmq/multi (현행) | To-be |
|------|---------------------------|--------------------------|-------|
| duration 시간 | 10s | **5s** (`BENCH_MULTI_DURATION_SECONDS:-${BENCH_MULTI_MEASURE_SECONDS:-5}`) | 5s로 통일 |
| send_workers | auto¹ | **2** (`BENCH_MULTI_SEND_WORKERS:-2`) | auto¹로 통일 |
| STREAM send_workers | 4 (auto¹) | 3 (`MULTI_STREAM_SEND_WORKERS:-3`) | auto¹로 통일 |
| STREAM send_batch | 64 | 64 (`MULTI_STREAM_SEND_BATCH:-64`) | = |

### 6.3 리소스 모니터링 전용 (benchwithstreamcompare)

현재 구현은 환경 변수가 아닌 **CLI 옵션 / 스크립트 내부 변수**로 전달된다.

| 파라미터 | 설명 | 기본값 | 전달 대상 | 비고 |
|----------|------|--------|-----------|------|
| `RESOURCE_SAMPLE_MS` | 리소스 샘플링 간격 (ms) | 500 | `start_process_resource_monitor()` (별도 모니터 프로세스) | 벤치 바이너리 인자가 아님 |
| `--latency-sample-rate <N>` | 레이턴시 히스토그램 샘플 비율 | 100 | 클라이언트 바이너리 CLI 인자 | phase별 분기: throughput phase → 강제 0, latency phase → `LATENCY_SAMPLE_RATE` 값 사용 |

> `--latency-sample-rate` 동작 상세:
> - 스크립트 변수 `LATENCY_SAMPLE_RATE=100` (기본값)
> - `run_stack_phase()`에서 phase가 `throughput`이면 `latency_sample_rate=0`으로 강제 설정
> - phase가 `latency`이면 `LATENCY_SAMPLE_RATE` 값(기본 100) 사용
> - 즉, throughput phase에서는 레이턴시 퍼센타일을 수집하지 않음

---

## 7. 비교 테이블 통합 포맷

### 7.1 2-Way 비교 (baseline vs current, zlink vs libzmq)

```
### Transport: tcp

| Size   | Metric     |     baseline |      current |  Diff (%) |
|--------|------------|-------------|-------------|-----------|
| 64B    | Throughput |  120.00 Kmsg/s |  135.00 Kmsg/s |  +12.50% |
| 64B    | Latency    |    45.23 us |    40.10 us |  +11.34% |
```

### 7.2 다중 스택 비교 (3개 이상 라이브러리: 랭킹 포맷)

routercompare, streamcompare 등 3개 이상 스택을 비교할 때는 다중 스택 랭킹 포맷을 사용한다.

```
## Phase: throughput | Size: 64B

| Rank | Stack       | Pass | Peak BPS    | Med BPS     | p95(us) |
|------|-------------|------|-------------|-------------|---------|
| 1    | zlink       | 3/3  | 1.2 Gbps   | 1.1 Gbps   | 45 us   |
| 2    | libzmq      | 3/3  | 0.9 Gbps   | 0.8 Gbps   | 62 us   |
| 3    | gRPC        | 3/3  | 0.7 Gbps   | 0.6 Gbps   | 85 us   |
```

> CV%, Gate 컬럼은 사용하지 않는다 (5.2절 참조).

---

## 8. 메시지 크기 및 트랜스포트 매트릭스

### 8.1 표준 메시지 크기

토폴로지에 따라 두 가지 크기 세트를 사용한다.

| 토폴로지 | 크기 세트 | 설명 |
|----------|----------|------|
| **Server-to-Server** | `[64, 256, 1024, 65536, 131072, 262144]` | 6개, 세밀한 페이로드 분포 측정 |
| **Client-to-Server** | `[64, 1024, 65536]` | 3개, 다수 클라이언트 부하 환경에서 핵심 구간만 측정 |

- 64B: 최소 메시지, overhead 비율 측정
- 256B, 1024B: 일반적인 RPC 페이로드 크기 (Server-to-Server 전용)
- 65536B, 131072B, 262144B: 대용량 전송 처리량 측정 (131072B, 262144B는 Server-to-Server 전용)

### 8.2 트랜스포트 매트릭스

#### zlink 자체 벤치마크 (benchwithzlink/multi)

| 패턴 유형 | 트랜스포트 | 비고 |
|-----------|-----------|------|
| ZMQ 소켓 패턴 (DEALER, ROUTER, PUBSUB) | tcp, tls, ws, wss | multi 기준 |
| STREAM | tcp, tls, ws, wss | |
| GATEWAY 종단간 (end-to-end) | tcp, tls, ws, wss | |
| GATEWAY discovery / registry | tcp | 고정 |
| SPOT 종단간 (end-to-end) | tcp, tls, ws, wss | |
| SPOT ↔ SPOT_NODE | inproc | 고정 (inproc 전용) |

#### 외부 라이브러리 비교 (benchwithzmq, benchwithroutercompare)

| 비교 대상 | 트랜스포트 | 비고 |
|-----------|-----------|------|
| zlink vs libzmq (multi) | tcp | tcp만 비교 |
| zlink vs libzmq vs gRPC (routercompare) | tcp | tcp만 비교 |

---

## 9. 벤치마크별 통합 적용 가이드

### 9.1 benchwithzlink/multi (기준)

이미 통합 표준을 따르고 있다. 변경 없음.

- RESULT 라인 포맷: 준수
- C++ 측정 페이즈: warmup → settle → duration (3단계)
- 메트릭: throughput, latency (Tier 1)
- 통계: median (엔트리 `run_benchmarks.sh` 기본 1 run, `run_comparison.py` 직접 실행 시 3 runs)
- 환경 변수: `BENCH_MULTI_*` 네이밍 준수

### 9.2 benchwithroutercompare

**현재 차이점:**
- 시간 기반 측정 (`BENCH_DURATION_SECONDS`, 기본 5s) — `BENCH_MULTI_DURATION_SECONDS`로 통일 예정
- 리소스 메트릭(CPU%, RSS)을 Python 스크립트 내부에서 수집하여 별도 포맷으로 출력
- 3-way 비교 테이블 구조
- `BENCH_PORT`, `BENCH_DURATION_SECONDS`, `BENCH_SETTLE_MS` 등 독자 환경 변수
- 기본 runs=5 (`run_benchmarks.sh`)

**통합 방향:**
1. C++ 바이너리 RESULT 라인은 이미 표준 포맷 → 유지
2. 리소스 메트릭을 RESULT 라인 확장 포맷(Tier 3)으로 출력하도록 개선
3. 환경 변수 매핑 추가:
   - `BENCH_DURATION_SECONDS` → `BENCH_MULTI_DURATION_SECONDS` (통일)
   - `BENCH_SETTLE_MS` → `BENCH_MULTI_SETTLE_MS` (통일)
4. 비교 테이블: 3-way 유지하되, 동일 Markdown 포맷 규칙 적용

### 9.3 benchwithstreamcompare

**현재 차이점:**
- 독자 CSV/JSON 결과 포맷 (RESULT 라인이 아닌 CSV metrics.csv)
- CV%, gate, confidence 등 고급 안정성 분석 (삭제 대상)
- 랭킹 기반 비교 (2-way가 아닌 N-way)
- BPS(bytes/sec) 단위 throughput
- p50/p95/p99 레이턴시 백분위수

**통합 방향:**
1. C++ 바이너리에서 표준 RESULT 라인 출력 추가 (기존 CSV 유지 가능)
2. throughput는 msgs/sec 기준, throughput_bps는 Tier 2 확장 메트릭으로 병행
3. latency_p95, latency_p99는 Tier 2 확장 메트릭으로 RESULT 라인에 포함
4. **CV%/gate/confidence 분석 로직 삭제** - median 집계로 단일화
5. 랭킹 테이블에서 CV%, Gate 컬럼 제거
6. 환경 변수 네이밍을 `BENCH_MULTI_*` 스타일로 통일

### 9.4 benchwithzmq (multi만 해당)

> benchwithzmq/single은 이 문서의 범위 밖이다.

**현재 차이점:**
- libzmq API 직접 호출 (zlink_* wrapper가 아닌 zmq_* 직접)
- 별도 캐시 파일 (`std_zmq_*_cache_*.json`)

**통합 방향:**
1. RESULT 라인 포맷: 이미 표준 → 유지
2. multi 패턴: `BENCH_MULTI_*` 환경 변수 체계와 정렬
3. 캐시 파일 네이밍: `<suite>_cache_<platform>-<arch>.json` 통일

---

## 10. 구현 체크리스트

### Phase 1: 즉시 적용 (코드 변경 없음)

- [ ] 모든 스위트의 `run_comparison.py`에서 동일 diff% 계산 공식 확인
- [ ] 환경 변수 문서화 (이 문서로 대체)
- [ ] 결과 파일 네이밍 컨벤션 통일: `results/YYYYMMDD/bench_<platform>_<pattern>_<timestamp>.txt`

### Phase 2: 출력 포맷 정렬 및 파서 확장

- [ ] `common/multi_e2e_metrics.py` `parse_results()`: Tier 2/3 메트릭 키 파싱 추가 (현행은 throughput/latency만 인식)
- [ ] `run_comparison.py` `parse_result_line()`: 동일 확장
- [ ] benchwithroutercompare: Tier 3 리소스 메트릭을 RESULT 라인으로 출력
- [ ] benchwithstreamcompare: 표준 RESULT 라인 추가 출력 (기존 CSV와 병행)
- [ ] 각 스위트 run_comparison.py가 표준 RESULT 라인을 파싱하도록 통합 파서 공유

### Phase 3: 환경 변수 통일

- [ ] `BENCH_DURATION_SECONDS` → `BENCH_MULTI_DURATION_SECONDS` 통일
- [ ] `BENCH_MULTI_MEASURE_SECONDS` → `BENCH_MULTI_DURATION_SECONDS` 통일 (호환 별칭 유지)
- [ ] `BENCH_SETTLE_MS` → `BENCH_MULTI_SETTLE_MS` 별칭 지원
- [ ] streamcompare 전용 변수를 `BENCH_MULTI_*` 네이밍으로 마이그레이션

### Phase 4: 공용 유틸리티 추출

- [ ] `core/bench/common/` 디렉토리에 공용 Python 파서/집계 모듈 배치
- [ ] RESULT 라인 파싱, median 계산, diff% 포맷, 테이블 렌더링 공유
- [ ] C++ 공용 헤더 정리 (bench_common.hpp 기준)

---

## 부록 A: RESULT 라인 파싱 레퍼런스

```python
def parse_result_line(line):
    """Parse a standard RESULT line into components."""
    if not line.startswith("RESULT,"):
        return None
    parts = line.strip().split(",")
    if len(parts) < 7:
        return None
    return {
        "lib": parts[1],
        "pattern": parts[2],
        "transport": parts[3],
        "size": int(parts[4]),
        "metric": parts[5].strip().lower(),
        "value": float(parts[6]),
    }
```

## 부록 B: 메트릭 집계 레퍼런스

```python
import statistics

def aggregate_runs(values):
    """Standard aggregation: median of N runs."""
    if not values:
        return 0.0
    return statistics.median(values)

def compute_diff_pct(base, current, metric_type):
    """
    Compute improvement percentage.
    Positive = current is better.
    Returns "N/A" when base <= 0 (cannot compute ratio).
    """
    if base <= 0:
        return "N/A"
    if metric_type == "throughput":
        return ((current - base) / base) * 100.0
    else:  # latency: lower is better
        return ((base - current) / base) * 100.0
```

## 부록 C: 측정 시간 기준

다중 클라이언트 시간 기반 테스트에서는 `BENCH_MULTI_DURATION_SECONDS`가 기준이며,
메시지 수는 시스템이 처리하는 만큼 자동 결정된다.

#### 현행 기본값 (As-is)

| 스위트 | duration 기본값 | 현행 환경 변수 | 비고 |
|--------|----------------|---------------|------|
| benchwithzlink/multi | **10s** | `BENCH_MULTI_MEASURE_SECONDS` | To-be에서 5s로 변경 |
| benchwithzmq/multi | 5s | `BENCH_MULTI_DURATION_SECONDS:-${BENCH_MULTI_MEASURE_SECONDS:-5}` | = |
| benchwithroutercompare | 5s | `BENCH_DURATION_SECONDS` | = |
| benchwithstreamcompare | 5s | `DURATION=5` | = |

> To-be: 모든 스위트의 duration 시간을 **5s**로 통일하고, 환경 변수를 `BENCH_MULTI_DURATION_SECONDS`로 통일한다.
