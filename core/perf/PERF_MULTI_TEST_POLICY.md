# zlink Multi Performance Test Policy

> **적용 범위**: `core/perf/multi`
> **Policy Version**: 1.0
> **Date**: 2026-02-23
> **Scope**: zlink multi-client 성능 테스트 정책

---

## 1. 측정 기준

| 항목 | 기준 |
|------|------|
| 측정 모델 | time-based: throughput(duration) + latency(duration) |
| throughput | `recv_count / duration_seconds` |
| latency | duration phase (패턴별 제수 적용) |
| 대표값 | median (runs > 1) |
| 기본 runs | 3 |
| 결과 출력 | RESULT line |

---

## 2. 운영 모드

| 모드 | 목적 | baseline | 기본 runs | 판정 |
|------|------|----------|-----------|------|
| Observe | 수치 수집 | 불필요 | 3 | 실행 오류만 fail |
| Trend | 회귀 감지 | rolling (최근 N회 median) | 3 | threshold 초과 시 warning |
| Gate | 릴리즈 승인 | 고정 (릴리즈 시점 저장) | 5 | threshold 초과 시 fail |

- 기본 모드: **Observe**
- Baseline comparison은 Trend/Gate 모드에서만 수행한다.
- Rolling baseline(최근 N회 median)을 기본으로 하며, 고정 baseline은 릴리즈 시점에만 사용한다.

### 2.1 임계치 기본값

| 메트릭 | warning | fail |
|--------|---------|------|
| throughput | -10% | -15% |
| latency | +10% | +15% |

- Observe: 임계치 미적용
- Trend: warning만 적용
- Gate: warning + fail 적용
- 패턴/transport별 개별 임계치는 설정 파일에서 override 가능

---

## 3. 테스트 유효성 기준

### 3.1 결과 상태 분류

| 상태 | 조건 | 집계 |
|------|------|------|
| success | RESULT line 정상 출력 | 유효 결과 |
| unsupported | 패턴-transport 조합 미지원 | 결과 제외, fail 아님 |
| skip | 환경 미충족 (OS, 아키텍처, nofile limit 등) | 결과 제외, fail 아님 |
| fail | timeout / no_data / non-zero exit | 무효 처리 |

### 3.2 유효성 규칙

1. 모든 `pattern/transport/size` 조합에서 RESULT line이 출력되어야 한다.
2. `unsupported`는 fail 집계에서 제외한다.
3. `skip`은 fail 집계에서 제외한다.
4. runs > 1인 경우 대표값은 **median**을 사용한다.

### 3.3 실행 순서

- 순회 순서: pattern → transport → size (고정)
- run 간 cooldown: 3000ms (`BENCH_MULTI_RUN_COOLDOWN_MS`)

---

## 4. 결과 산출물

### 4.1 결과 파일 형식

결과 파일은 META 헤더와 RESULT 본문으로 구성된다.

```text
META,os,Linux 6.6.87.2-microsoft-standard-WSL2
META,cpu,AMD Ryzen 9 7950X
META,cores,32
META,build,Release
META,commit,abc1234
META,utc,2026-02-23T14:30:00Z
META,load_avg,0.52 0.48 0.45
META,mode,observe
META,runs,3
META,clients,100
RESULT,current,MULTI_DEALER_DEALER,tcp,64,throughput,150000.00
RESULT,current,MULTI_DEALER_DEALER,tcp,64,latency,45.23
RESULT,current,MULTI_DEALER_DEALER,tcp,256,throughput,135000.00
RESULT,current,MULTI_DEALER_DEALER,tcp,256,latency,52.10
```

| 라인 유형 | 형식 | 설명 |
|-----------|------|------|
| `META` | `META,<key>,<value>` | 실행 환경 메타데이터 |
| `RESULT` | `RESULT,<lib>,<pattern>,<transport>,<size>,<metric>,<value>` | 측정 결과 |

### 4.2 META 필수 키

| 키 | 필수 | 설명 |
|----|------|------|
| `os` | MUST | OS 및 커널 버전 |
| `cpu` | MUST | CPU 모델명 |
| `cores` | MUST | 논리 코어 수 |
| `build` | MUST | 빌드 타입 (Release/Debug) |
| `commit` | MUST | git commit SHA |
| `utc` | MUST | 실행 시각 (ISO 8601) |
| `load_avg` | SHOULD | 실행 시점 load average |
| `mode` | MUST | 운영 모드 (observe/trend/gate) |
| `runs` | MUST | 반복 횟수 |
| `clients` | MUST | 클라이언트 소켓 수 |

### 4.3 저장 구조

```text
core/perf/results/
├── YYYYMMDD/                                             # 일별 결과
│   ├── bench_<platform>_YYYYMMDD_HHMMSS_multi[_<tag>].txt
│   └── ...
└── baselines/                                            # baseline 저장소
    ├── latest_multi.txt                                  # 최근 고정 baseline (symlink)
    ├── v1.4.0_multi.txt                                  # 릴리즈 시점 고정 baseline
    └── ...
```

| 경로 | 용도 | 생성 |
|------|------|------|
| `results/YYYYMMDD/` | 일별 측정 결과 | `--result` 옵션 |
| `results/baselines/<version>_multi.txt` | 릴리즈 시점 고정 baseline | `--save-baseline <version>` |
| `results/baselines/latest_multi.txt` | 최근 고정 baseline 참조 | baseline 저장 시 자동 갱신 |

### 4.4 Baseline 관리

| 모드 | baseline 소스 | 생성 방법 |
|------|--------------|-----------|
| Observe | 없음 | — |
| Trend | rolling (최근 N회 median) | `results/YYYYMMDD/`에서 자동 조회 |
| Gate | 고정 (`results/baselines/`) | `--save-baseline <version>` |

- Rolling baseline: 동일 `pattern/transport/size` 조합의 최근 N회(기본 10) 결과에서 median을 산출한다.
- 고정 baseline: 릴리즈 시점에 생성하며, 결과 파일과 동일한 형식(META + RESULT)을 사용한다.
- baseline 비교 시 동일 `pattern/transport/size` 키로 매칭한다.

### 4.5 보존 정책

| 대상 | 보존 기간 |
|------|-----------|
| 일별 결과 (`results/YYYYMMDD/`) | 90일 (자동 정리 가능) |
| 고정 baseline (`results/baselines/`) | 영구 (릴리즈 태그 연동) |
| rolling baseline 참조 범위 | 최근 10회 (기본값, `BENCH_ROLLING_N` 으로 override) |

---

## 5. 실행 방법

### 5.1 스크립트 실행

```bash
core/perf/run_benchmarks_multi.sh [options]
```

### 5.2 실행 옵션

| 옵션 | 설명 | 기본값 |
|------|------|--------|
| `--pattern NAME` | 측정할 패턴 (쉼표 구분 가능) | 전체 MULTI_* 패턴 |
| `--build-dir PATH` | 빌드 디렉터리 경로 | `core/build/<platform>-<arch>` |
| `--runs N` | 패턴/transport/size 조합당 반복 횟수 | 3 |
| `--reuse-build` | 기존 빌드 디렉터리 재사용 | on (기본) |
| `--pin-cpu` | Linux taskset CPU 고정 | off |
| `--io-threads N` | context I/O threads 수 | 0 |
| `--msg-sizes LIST` | 메시지 크기 목록 (쉼표 구분) | `64,256,1024,65536,131072,262144` |
| `--transports LIST` | transport 목록 (쉼표 구분) | `tcp,tls,ws,wss` |
| `--output PATH` | 결과를 파일에 동시 출력 (tee) | stdout만 |
| `--result` | `core/perf/results/YYYYMMDD/` 아래 자동 저장 | off |
| `--results-dir PATH` | 결과 저장 루트 디렉터리 override | `core/perf/results` |
| `--results-tag NAME` | 결과 파일명에 태그 추가 | 없음 |
| `--multi-warmup-seconds N` | warmup 시간(초) | 3 |
| `--multi-duration-seconds N` | 측정 시간(초) | 5 |
| `--multi-clients N` | 클라이언트 소켓 수 | 100 (STREAM: 10000) |
| `--multi-inflight N` | 클라이언트당 in-flight 메시지 수 | 1 (추후 테스트 후 재결정) |
| `--multi-hwm N` | 소켓 HWM | 100000 |
| `--multi-sndtimeo-ms N` | 송신 타임아웃(ms) | 5000 |
| `--multi-rcvtimeo-ms N` | 수신 타임아웃(ms) | 5000 |
| `--multi-connect-concurrency N` | 동시 연결 수 | auto |
| `--multi-drain-ms N` | drain 대기(ms) | 패턴별 기본값 |

### 5.3 실행 예시

```bash
# 전체 멀티 패턴 실행
core/perf/run_benchmarks_multi.sh

# 특정 패턴만 실행
core/perf/run_benchmarks_multi.sh --pattern MULTI_STREAM

# 여러 패턴
core/perf/run_benchmarks_multi.sh --pattern MULTI_DEALER_DEALER,MULTI_PUBSUB

# 클라이언트 수/메시지 크기 제한
core/perf/run_benchmarks_multi.sh --multi-clients 1000 --msg-sizes 64,1024

# 5회 반복, CPU 고정, 결과 자동 저장
core/perf/run_benchmarks_multi.sh --runs 5 --pin-cpu --result

# 측정 시간 조정
core/perf/run_benchmarks_multi.sh --multi-warmup-seconds 5 --multi-duration-seconds 10
```

### 5.4 바이너리 직접 실행

개별 벤치마크 바이너리를 직접 실행할 수 있다.

```bash
<binary> <lib_name> <transport> <size>
```

```bash
# 예시
./core/build/linux-x64/bin/comp_current_multi_dealer_dealer current tcp 1024
```

| 인자 | 설명 |
|------|------|
| `lib_name` | 라이브러리 식별자 (`current`) |
| `transport` | `tcp`, `tls`, `ws`, `wss` |
| `size` | 메시지 크기(bytes) |

---

## 6. 출력 형식

### 6.1 바이너리 RESULT line

각 바이너리는 `pattern/transport/size` 조합마다 throughput, latency 두 줄을 stdout에 출력한다.

```text
RESULT,current,MULTI_DEALER_DEALER,tcp,1024,throughput,150000.00
RESULT,current,MULTI_DEALER_DEALER,tcp,1024,latency,45.23
```

| 필드 | 설명 |
|------|------|
| `lib` | 라이브러리 식별자 (`current`) |
| `pattern` | `MULTI_DEALER_DEALER`, `MULTI_STREAM` 등 |
| `transport` | `tcp`, `tls`, `ws`, `wss` |
| `size` | 메시지 크기(bytes) |
| `metric` | `throughput` 또는 `latency` |
| `value` | 수치 값 (소수점 2자리) |

### 6.2 스크립트 결과 테이블

`run_benchmarks_multi.sh` 실행 시 패턴/transport별로 markdown table이 출력된다.

```text
## PATTERN: MULTI_DEALER_DEALER

### Transport: tcp
| Size     |       Throughput |      Latency |
|----------|------------------|--------------|
| 64B      |   150.00 Kmsg/s  |    45.23 us  |
| 1024B    |   120.30 Kmsg/s  |    52.10 us  |
| 65536B   |    35.50 Kmsg/s  |   180.44 us  |
```

- throughput 단위: `Kmsg/s` (msgs/sec / 1000)
- latency 단위: `us` (마이크로초)
- transport 미지원 시: `N/A`

### 6.3 진행 로그

실행 중 각 조합의 진행 상황이 출력된다.

```text
  > Benchmarking current for MULTI_DEALER_DEALER...
    Testing tcp | 64B,1024B,65536B: 1 2 3 Done
    Testing tls | 64B,1024B,65536B: 1 2 3 Done
```

- 실패 발생 시: `(failures=1) Done`
- transport 미지원 시: `unsupported Done`

### 6.4 실패 요약

실패가 있는 경우 마지막에 요약이 출력된다.

```text
## Failures
- MULTI_STREAM current wss 65536B: timeout
```

### 6.5 결과 파일 저장

`--result` 옵션 사용 시 결과가 파일로 저장된다. 파일 형식 및 저장 구조는 섹션 4를 참조한다.

```text
core/perf/results/YYYYMMDD/bench_<platform>_YYYYMMDD_HHMMSS_multi[_<tag>].txt
```

---

## 7. Test Phase

```text
[warmup] -> [settle] -> [duration] -> [drain] -> [size_transition_drain]
```

| Phase | 방식 | 기본값 | 환경 변수 |
|-------|------|--------|-----------|
| warmup | time-based | 3s | `BENCH_MULTI_WARMUP_SECONDS` |
| settle | time-based | 500ms | `BENCH_MULTI_SETTLE_MS` |
| duration | time-based | 5s | `BENCH_MULTI_DURATION_SECONDS` |
| drain | time-based | 패턴별 | `BENCH_MULTI_DRAIN_MS` |
| size_transition_drain | time-based | 300ms | `BENCH_MULTI_SIZE_TRANSITION_DRAIN_MS` |

### 7.1 drain 패턴별 기본값

| 패턴 | drain 기본값 |
|------|-------------|
| MULTI_DEALER_DEALER | 300ms |
| MULTI_DEALER_ROUTER | 300ms |
| MULTI_ROUTER_ROUTER | 300ms |
| MULTI_PUBSUB | 300ms |
| MULTI_STREAM | 300ms |
| MULTI_GATEWAY | 0ms |
| MULTI_SPOT | 0ms |

### 7.2 warmup 모드

| 모드 | 설명 | 환경 변수 |
|------|------|-----------|
| passive (기본) | 송신 비활성 상태에서 시간 대기 | `BENCH_MULTI_ACTIVE_WARMUP=0` |
| active | 송신/수신 활성 상태로 워밍업 | `BENCH_MULTI_ACTIVE_WARMUP=1` |

active warmup 시 pre-measure drain: `BENCH_MULTI_WARMUP_DRAIN_MS` (기본 `max(BENCH_MULTI_DRAIN_MS, 1000)`)

---

## 8. Throughput 측정

1. duration 구간의 수신량으로 계산한다.
2. `throughput = recv_count / duration_seconds`
3. warmup/drain/size_transition_drain 구간의 데이터는 계산에서 제외한다.

---

## 9. Latency 측정

latency는 duration 구간에서 throughput과 동시에 측정한다.

### 9.1 패턴별 divisor 규칙

| 유형 | divisor | 적용 패턴 |
|------|---------|-----------|
| 양방향 RTT | `roundtrip_count * 2` | MULTI_DEALER_*, MULTI_ROUTER_*, MULTI_STREAM |
| 단방향 | `received_count` | MULTI_PUBSUB |
| 단방향 멀티홉 | `received_count` | MULTI_GATEWAY, MULTI_SPOT |

### 9.2 계산식

- RTT: `latency_us = elapsed_us / (roundtrip_count * 2)`
- 단방향: `latency_us = elapsed_us / received_count`

- warmup/drain 구간의 데이터는 계산에서 제외한다.

---

## 10. Metric Tiers

### 10.1 Tier 1: 필수

| 메트릭 | 단위 | 계산 방식 |
|--------|------|-----------|
| `throughput` | msgs/sec | `recv_count / duration_seconds` |
| `latency` | us | 패턴별 divisor 규칙 적용 (섹션 9.1) |

Tier 1 메트릭이 누락되면 해당 조합은 fail로 처리한다.

### 10.2 Tier 2: 권장

| 메트릭 | 단위 | 설명 |
|--------|------|------|
| `latency_p95` | us | 95th percentile 레이턴시 |
| `latency_p99` | us | 99th percentile 레이턴시 |
| `throughput_bps` | bytes/sec | `throughput * msg_size` |
| `connect_ms` | ms | 전체 클라이언트 연결 완료 시간 |
| `ready_ms` | ms | 연결 후 준비 완료 대기 시간 |

### 10.3 Tier 3: 선택 (리소스 모니터링)

| 메트릭 | 단위 | 수집 방식 |
|--------|------|-----------|
| `server_cpu_pct` | % | `/proc/[pid]/stat` 샘플링 |
| `server_rss_mb` | MB | `/proc/[pid]/status` VmRSS |
| `client_cpu_pct` | % | `/proc/[pid]/stat` 샘플링 |
| `client_rss_mb` | MB | `/proc/[pid]/status` VmRSS |

Tier 2/3 메트릭 누락 시 해당 메트릭은 `N/A`로 표시하며, 결과 유효성에 영향을 주지 않는다.

---

## 11. Pattern & Transport Matrix

### 11.1 지원 패턴

MULTI_DEALER_DEALER, MULTI_DEALER_ROUTER, MULTI_ROUTER_ROUTER, MULTI_PUBSUB, MULTI_GATEWAY, MULTI_SPOT, MULTI_STREAM

### 11.2 표준 메시지 크기

```text
[64, 256, 1024, 65536, 131072, 262144]
```

### 11.3 transport

| 패턴군 | transport |
|--------|-----------|
| MULTI_DEALER / MULTI_ROUTER / MULTI_PUBSUB | tcp, tls, ws, wss |
| MULTI_STREAM | tcp, tls, ws, wss |
| MULTI_GATEWAY (종단간) | tcp, tls, ws, wss |
| MULTI_GATEWAY (discovery/registry) | tcp (고정) |
| MULTI_SPOT (종단간) | tcp, tls, ws, wss |
| MULTI_SPOT (SPOT_NODE 간) | inproc (고정) |

---

## 12. Environment Variables

### 12.1 공통

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `BENCH_DEBUG` | 디버그 로그 | unset |
| `BENCH_IO_THREADS` | context I/O threads | 0 |
| `BENCH_MSG_SIZES` | 테스트 size 목록 | `64,256,1024,65536,131072,262144` |
| `BENCH_TRANSPORTS` | 테스트 transport 목록 | `tcp,tls,ws,wss` |
| `BENCH_TASKSET` | Linux CPU pinning (`1`로 활성화) | 0 |
| `BENCH_FAIL_FAST` | 실패 시 즉시 중단 (`1`로 활성화) | 0 |

### 12.2 Phase 제어

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `BENCH_MULTI_WARMUP_SECONDS` | warmup 시간(초) | 3 |
| `BENCH_MULTI_DURATION_SECONDS` | 측정 시간(초) | 5 |
| `BENCH_MULTI_SETTLE_MS` | settle 대기(ms) | 500 |
| `BENCH_MULTI_DRAIN_MS` | drain 대기(ms) | 패턴별 |
| `BENCH_MULTI_SIZE_TRANSITION_DRAIN_MS` | size 전환 drain(ms) | 300 |
| `BENCH_MULTI_ACTIVE_WARMUP` | active warmup 활성화 | 0 |
| `BENCH_MULTI_WARMUP_DRAIN_MS` | active warmup 후 drain(ms) | `max(drain_ms, 1000)` |

### 12.3 클라이언트 제어

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `BENCH_MULTI_CLIENTS` | 클라이언트 소켓 수 | 100 (STREAM: 10000) |
| `BENCH_MULTI_INFLIGHT` | 클라이언트당 in-flight 메시지 수 | 1 (추후 테스트 후 재결정) |
| `BENCH_MULTI_STREAM_MAX_INFLIGHT_BYTES` | STREAM size별 inflight auto-cap(bytes) | 33554432 (32MiB) |
| `BENCH_MULTI_HWM` | 소켓 HWM | 100000 |
| `BENCH_MULTI_CONNECT_CONCURRENCY` | 동시 연결 수 | auto (`clients>=10000 ? 1024 : 128`) |
| `BENCH_MULTI_CONNECT_READY_TIMEOUT_MS` | 연결 준비 타임아웃(ms) | 5000 |

### 12.4 송수신 제어

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `BENCH_MULTI_SEND_WORKERS` | 송신 워커 스레드 수 | auto (`clients>=10000 ? 4 : 1`) |
| `BENCH_MULTI_SEND_BACKOFF_US` | 송신 블록 시 backoff(us) | 20 |
| `BENCH_MULTI_RECV_BATCH` | 수신 배치 크기 | 64 |
| `BENCH_MULTI_BLOCKING_SEND` | 블로킹 전송 모드 | 0 |
| `BENCH_MULTI_SNDTIMEO_MS` | 송신 타임아웃(ms) | 5000 |
| `BENCH_MULTI_RCVTIMEO_MS` | 수신 타임아웃(ms) | 5000 |
| `BENCH_MULTI_MONITOR_HWM` | 모니터 소켓 HWM | 200000 |
| `BENCH_SERVER_RECV_THREADS` | 서버 수신 스레드 수 | 1 |

### 12.5 기타

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `BENCH_MAX_SOCKETS` | context max sockets | auto |
| `BENCH_MULTI_RUN_COOLDOWN_MS` | run 간 cooldown(ms) | 3000 |
| `BENCH_MULTI_ATTEMPTS` | 실패 시 재시도 횟수 | 2 |
| `BENCH_MULTI_STREAM_ATTEMPTS` | STREAM 재시도 횟수 | 2 |
| `BENCH_SKIP_NOFILE_CHECK` | nofile limit 검사 생략 | 0 |

---

## Appendix: 계산 레퍼런스

```python
import statistics

def aggregate_runs(values):
    """runs > 1인 경우 대표값 산출"""
    if not values:
        return 0.0
    return statistics.median(values)

def throughput_msgs_per_sec(recv_count, duration_seconds):
    return recv_count / max(1, duration_seconds)

def latency_rtt_us(elapsed_us, roundtrip_count):
    """MULTI_DEALER_*, MULTI_ROUTER_*, MULTI_STREAM"""
    return elapsed_us / max(1, roundtrip_count * 2)

def latency_oneway_us(elapsed_us, count):
    """MULTI_PUBSUB, MULTI_GATEWAY, MULTI_SPOT: count=received_count"""
    return elapsed_us / max(1, count)
```
