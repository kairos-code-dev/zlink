# zlink Single Performance Test Policy

> **적용 범위**: `core/perf/single`
> **Policy Version**: 1.0
> **Date**: 2026-02-23
> **Scope**: zlink single-client 성능 테스트 정책

---

## 1. 측정 기준

| 항목 | 기준 |
|------|------|
| 측정 모델 | hybrid: throughput(duration) + latency(count) |
| throughput | `recv_count / duration_seconds` |
| latency | count phase (패턴별 제수 적용) |
| 대표값 | median (runs > 1) |
| 결과 출력 | RESULT line |

---

## 2. 운영 모드

| 모드 | 목적 | baseline | 기본 runs | 판정 |
|------|------|----------|-----------|------|
| Observe | 수치 수집 | 불필요 | 1 | 실행 오류만 fail |
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
| skip | 환경 미충족 (OS, 아키텍처 등) | 결과 제외, fail 아님 |
| fail | timeout / no_data / non-zero exit | 무효 처리 |

### 3.2 유효성 규칙

1. 모든 `pattern/transport/size` 조합에서 RESULT line이 출력되어야 한다.
2. `unsupported`는 fail 집계에서 제외한다.
3. `skip`은 fail 집계에서 제외한다.
4. runs > 1인 경우 대표값은 **median**을 사용한다.

### 3.3 실행 순서

- 순회 순서: pattern → transport → size (고정)
- cooldown: 없음

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
META,runs,1
RESULT,current,PAIR,tcp,64,throughput,523401.23
RESULT,current,PAIR,tcp,64,latency,12.35
RESULT,current,PAIR,tcp,256,throughput,480123.45
RESULT,current,PAIR,tcp,256,latency,14.20
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

### 4.3 저장 구조

```text
core/perf/results/
├── YYYYMMDD/                                        # 일별 결과
│   ├── bench_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt
│   └── ...
└── baselines/                                       # baseline 저장소
    ├── latest.txt                                   # 최근 고정 baseline (symlink)
    ├── v1.4.0.txt                                   # 릴리즈 시점 고정 baseline
    └── ...
```

| 경로 | 용도 | 생성 |
|------|------|------|
| `results/YYYYMMDD/` | 일별 측정 결과 | `--result` 옵션 |
| `results/baselines/<version>.txt` | 릴리즈 시점 고정 baseline | `--save-baseline <version>` |
| `results/baselines/latest.txt` | 최근 고정 baseline 참조 | baseline 저장 시 자동 갱신 |

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
core/perf/run_benchmarks.sh [options]
```

### 5.2 실행 옵션

| 옵션 | 설명 | 기본값 |
|------|------|--------|
| `--pattern NAME` | 측정할 패턴 (쉼표 구분 가능) | `ALL` (전체 single 패턴) |
| `--build-dir PATH` | 빌드 디렉터리 경로 | `core/build/<platform>-<arch>` |
| `--runs N` | 패턴/transport/size 조합당 반복 횟수 | 1 |
| `--reuse-build` | 기존 빌드 디렉터리 재사용 (CMake 재실행 생략) | off |
| `--pin-cpu` | Linux taskset CPU 고정 | off |
| `--io-threads N` | context I/O threads 수 | 0 |
| `--msg-sizes LIST` | 메시지 크기 목록 (쉼표 구분) | `64,256,1024,65536,131072,262144` |
| `--transports LIST` | transport 목록 (쉼표 구분) | 패턴별 기본값 |
| `--output PATH` | 결과를 파일에 동시 출력 (tee) | stdout만 |
| `--result` | `core/perf/results/YYYYMMDD/` 아래 자동 저장 | off |
| `--results-dir PATH` | 결과 저장 루트 디렉터리 override | `core/perf/results` |
| `--results-tag NAME` | 결과 파일명에 태그 추가 | 없음 |

### 5.3 실행 예시

```bash
# 전체 패턴 실행
core/perf/run_benchmarks.sh

# 특정 패턴만 실행
core/perf/run_benchmarks.sh --pattern PAIR

# 여러 패턴
core/perf/run_benchmarks.sh --pattern PAIR,PUBSUB,STREAM

# 메시지 크기/transport 제한
core/perf/run_benchmarks.sh --pattern PAIR --msg-sizes 64,1024 --transports tcp,inproc

# 3회 반복, CPU 고정, 결과 자동 저장
core/perf/run_benchmarks.sh --runs 3 --pin-cpu --result

# 기존 빌드 재사용
core/perf/run_benchmarks.sh --reuse-build --pattern STREAM
```

### 5.4 바이너리 직접 실행

개별 벤치마크 바이너리를 직접 실행할 수 있다.

```bash
<binary> <lib_name> <transport> <size>
```

```bash
# 예시
./core/build/linux-x64/bin/bench_current_pair current tcp 1024
```

| 인자 | 설명 |
|------|------|
| `lib_name` | 라이브러리 식별자 (`current`) |
| `transport` | `tcp`, `inproc`, `ipc`, `tls`, `ws`, `wss` |
| `size` | 메시지 크기(bytes) |

---

## 6. 출력 형식

### 6.1 바이너리 RESULT line

각 바이너리는 `pattern/transport/size` 조합마다 throughput, latency 두 줄을 stdout에 출력한다.

```text
RESULT,current,PAIR,tcp,1024,throughput,523401.23
RESULT,current,PAIR,tcp,1024,latency,12.35
```

| 필드 | 설명 |
|------|------|
| `lib` | 라이브러리 식별자 (`current`) |
| `pattern` | `PAIR`, `PUBSUB`, `STREAM`, `GATEWAY` 등 |
| `transport` | `tcp`, `inproc`, `ipc`, `ws`, `wss`, `tls` |
| `size` | 메시지 크기(bytes) |
| `metric` | `throughput` 또는 `latency` |
| `value` | 수치 값 (소수점 2자리) |

### 6.2 스크립트 결과 테이블

`run_benchmarks.sh` 실행 시 패턴/transport별로 markdown table이 출력된다.

```text
## PATTERN: PAIR

### Transport: tcp
| Size   |       Throughput |     Latency |
|--------|------------------|-------------|
| 64B    |   523.40 Kmsg/s  |   12.35 us  |
| 256B   |   480.12 Kmsg/s  |   14.20 us  |
| 1024B  |   312.50 Kmsg/s  |   18.44 us  |
...
```

- throughput 단위: `Kmsg/s` (msgs/sec / 1000)
- latency 단위: `us` (마이크로초)
- transport 미지원 시: `N/A`

### 6.3 진행 로그

실행 중 각 조합의 진행 상황이 출력된다.

```text
  > Benchmarking current for PAIR...
    Testing tcp | 64B: 1 Done
    Testing tcp | 256B: 1 Done
    Testing inproc | 64B: 1 Done
```

- `--runs 3` 시: `1 2 3 Done`
- 실패 발생 시: `(failures=1) Done`
- timeout 발생 시: failure로 기록
- transport 미지원 시: `unsupported Done`

### 6.4 실패 요약

실패가 있는 경우 마지막에 요약이 출력된다.

```text
## Failures
- PAIR current ipc 64B: timeout
- STREAM current wss 65536B: no_data
```

### 6.5 결과 파일 저장

`--result` 옵션 사용 시 결과가 파일로 저장된다. 파일 형식 및 저장 구조는 섹션 4를 참조한다.

```text
core/perf/results/YYYYMMDD/bench_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt
```

---

## 7. Test Phase

```text
[warmup] -> [settle] -> [throughput] -> [drain] -> [latency]
```

| Phase | 방식 | 기본값 | 환경 변수 |
|-------|------|--------|-----------|
| warmup | time-based | 3s | `BENCH_SINGLE_WARMUP_SECONDS` |
| settle | time-based | 300ms | `BENCH_SINGLE_SETTLE_MS` |
| throughput | time-based | 5s | `BENCH_SINGLE_DURATION_SECONDS` |
| drain | time-based | 300ms | `BENCH_SINGLE_DRAIN_MS` |
| latency | count-based | 패턴별 | `BENCH_LAT_COUNT` |

---

## 8. Throughput 측정

1. duration 구간의 수신량으로 계산한다.
2. `throughput = recv_count / duration_seconds`
3. warmup/settle/drain 구간의 데이터는 계산에서 제외한다.
4. STREAM 계열은 inflight 제어(`BENCH_STREAM_INFLIGHT`)를 적용한다.

---

## 9. Latency 측정

latency는 throughput과 분리된 count phase에서 측정한다.

### 9.1 패턴별 기본값

| 패턴 | LAT_COUNT | 제수(divisor) |
|------|-----------|---------------|
| PAIR | 500 | `lat_count * 2` |
| DEALER_DEALER | 500 | `lat_count * 2` |
| DEALER_ROUTER | 1000 | `lat_count * 2` |
| ROUTER_ROUTER | 1000 | `lat_count * 2` |
| ROUTER_ROUTER_POLL | 1000 | `lat_count * 2` |
| STREAM | 500 | `lat_count * 2` |
| STREAM_LEN32BE | 500 | `lat_count * 2` |
| PUBSUB | 500 | `received_count` |
| GATEWAY | 200 | `lat_count` |
| SPOT | 200 | `lat_count` |

### 9.2 divisor 규칙

| 유형 | divisor | 적용 패턴 |
|------|---------|-----------|
| 양방향 RTT | `lat_count * 2` | PAIR, DEALER_*, ROUTER_*, STREAM* |
| 단방향 | `received_count` | PUBSUB |
| 단방향 멀티홉 | `lat_count` | GATEWAY, SPOT |

### 9.3 계산식

- RTT: `latency_us = elapsed_us / (lat_count * 2)`
- PUBSUB: `latency_us = elapsed_us / received_count`
- GATEWAY/SPOT: `latency_us = elapsed_us / lat_count`

---

## 10. Pattern & Transport Matrix

### 10.1 지원 패턴

PAIR, PUBSUB, DEALER_DEALER, DEALER_ROUTER, ROUTER_ROUTER, ROUTER_ROUTER_POLL, STREAM, GATEWAY, SPOT

### 10.2 표준 메시지 크기

```text
[64, 256, 1024, 65536, 131072, 262144]
```

### 10.3 transport

| 패턴군 | transport |
|--------|-----------|
| PAIR / PUBSUB / DEALER / ROUTER | tcp, inproc, ipc (Windows: tcp, inproc) |
| STREAM / GATEWAY / SPOT | tcp, tls, ws, wss |

---

## 11. Environment Variables

### 11.1 공통

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `BENCH_DEBUG` | 디버그 로그 | unset |
| `BENCH_IO_THREADS` | context I/O threads | 0 |
| `BENCH_MSG_SIZES` | 테스트 size 목록 | `64,256,1024,65536,131072,262144` |
| `BENCH_TRANSPORTS` | 테스트 transport 목록 | 패턴별 기본값 |
| `BENCH_TASKSET` | Linux CPU pinning (`1`로 활성화) | 0 |
| `BENCH_LAT_COUNT` | latency count override | 패턴별 기본값 |
| `BENCH_FAIL_FAST` | 실패 시 즉시 중단 (`1`로 활성화) | 0 |

### 11.2 Phase 제어

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `BENCH_SINGLE_WARMUP_SECONDS` | warmup 시간(초) | 3 |
| `BENCH_SINGLE_DURATION_SECONDS` | throughput 측정 시간(초) | 5 |
| `BENCH_SINGLE_SETTLE_MS` | settle 대기(ms) | 300 |
| `BENCH_SINGLE_DRAIN_MS` | drain 대기(ms) | 300 |

### 11.3 STREAM 전용

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `BENCH_STREAM_TIMEOUT_MS` | socket timeout | 5000 |
| `BENCH_STREAM_DRAIN_TIMEOUT_MS` | drain timeout | `max(io*6,30000)` |
| `BENCH_STREAM_INFLIGHT` | inflight window | 10 |
| `BENCH_STREAM_HWM` | HWM | 100000 |
| `BENCH_STREAM_SERVER_IO_THREADS` | server io threads | 4 |
| `BENCH_STREAM_CLIENT_THREADS` | client worker threads | auto |

### 11.4 기타

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `BENCH_SINGLE_TIMEOUT_SECONDS` | 프로세스 timeout | 120 |
| `BENCH_MAX_SOCKETS` | context max sockets | auto |

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

def latency_rtt_us(elapsed_us, lat_count):
    """PAIR, DEALER_*, ROUTER_*, STREAM*"""
    return elapsed_us / max(1, lat_count * 2)

def latency_oneway_us(elapsed_us, count):
    """PUBSUB: count=received_count, GATEWAY/SPOT: count=lat_count"""
    return elapsed_us / max(1, count)
```
