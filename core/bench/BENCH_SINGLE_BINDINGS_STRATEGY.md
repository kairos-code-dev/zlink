# Single & Bindings Benchmark Strategy

> **Reference Standard**: `benchwithzlink/single`, `benchwithzmq/single`, `bindings/bench`
> **Date**: 2026-02-23
> **Scope**: single-client benchmark suites + bindings benchmark suites
> **Out of Scope**: multi-client benchmark details (`BENCH_UNIFIED_STRATEGY.md`)

---

## 1. Overview

이 문서는 single/bindings 벤치 정책을 multi와 의미적으로 정렬하는 것을 목표로 한다.
핵심 정렬 원칙은 다음과 같다.

- `throughput`: duration 기반 측정
- `latency`: count 기반 측정 (패턴별 제수 유지)

### 1.1 현재/목표 요약

| 항목 | 현재(As-is) | 목표(To-be) |
|------|-------------|------------|
| 측정 모델 | count 기반 중심 | hybrid: throughput(duration) + latency(count) |
| throughput | `msg_count / elapsed` | `measure_recv / duration_seconds` |
| latency | count phase | count phase 유지 (multi와 동일 철학) |
| 기본 runs | 1 (공식 기본값) | 1 유지 |
| 비교 출력 | RESULT line + markdown table | 동일 |

### 1.2 통합 목표

1. single/bindings에서 `throughput`을 duration 기반으로 통일
2. `latency`는 count 기반으로 유지해 패턴별 의미를 보존
3. phase 구조를 multi 스타일로 재정렬
4. diff% edge case (`base <= 0`)를 `"N/A"`로 강제
5. fallback/no_data/timeout을 벤치 invalid로 명확히 규정

### 1.3 벤치 유효성 규칙 (MUST)

1. base/current는 동일 `pattern/transport/size` 조합을 수행해야 한다.
2. 결과 채택 시 timeout/no_data/non-zero exit가 1건이라도 있으면 invalid다.
3. 바인딩 벤치는 해당 바인딩 러너를 직접 실행해야 하며 core fallback은 invalid다.
4. `base <= 0`이면 diff는 반드시 `"N/A"`여야 한다.
5. 실행 모드(`measure_mode`), duration, 핵심 환경이 바뀌면 cache를 재생성해야 한다.

### 1.4 벤치 invalid 조건

- `--allow-core-fallback` 사용 또는 fallback hit 발생
- size/transport 조합별 RESULT 누락
- diff 계산 불가 케이스를 `0%` 등 숫자로 출력
- timeout/no_data/failure를 성공으로 처리

---

## 2. RESULT Line Protocol

### 2.1 필수 출력 형식

모든 벤치 바이너리/러너는 아래 형식을 stdout에 출력해야 한다.

```text
RESULT,<lib>,<pattern>,<transport>,<size>,throughput,<value>
RESULT,<lib>,<pattern>,<transport>,<size>,latency,<value>
```

| 필드 | 설명 |
|------|------|
| `lib` | `baseline`, `current`, `libzmq`, `zlink`, `binding` 등 |
| `pattern` | `PAIR`, `PUBSUB`, `STREAM`, `GATEWAY` 등 |
| `transport` | `tcp`, `inproc`, `ipc`, `ws`, `wss`, `tls` |
| `size` | 메시지 크기(bytes) |
| `metric` | `throughput` 또는 `latency` |
| `value` | 수치 값 |

### 2.2 PUBSUB 해석 규칙

PUBSUB `latency`는 RTT가 아니라 one-way 전달 지표다.

- `latency_pubsub_us = elapsed_us / received_count`

### 2.3 Bindings lib 필드

현행 bindings는 `RESULT,current,...`를 출력해도 비교는 러너 레벨에서 수행된다.
lib 필드 구분(`zlink`, `node` 등)은 선택 개선 항목으로 유지한다.

---

## 3. Unified Phase Policy (To-be)

single/bindings의 표준 phase는 아래와 같이 정의한다.

```text
[warmup_duration] -> [settle] -> [throughput_duration] -> [drain] -> [latency_count]
```

### 3.1 Phase 정의

| Phase | 목적 | 방식 | 기본값 | 환경 변수 |
|------|------|------|--------|-----------|
| warmup_duration | 연결/JIT/버퍼 워밍업 | time-based | 3s | `BENCH_SINGLE_WARMUP_SECONDS` |
| settle | 연결 안정화 | time-based | 300ms | `BENCH_SINGLE_SETTLE_MS` |
| throughput_duration | 처리량 측정 | time-based | 5s | `BENCH_SINGLE_DURATION_SECONDS` |
| drain | 잔여 메시지 배출 | time-based | 300ms | `BENCH_SINGLE_DRAIN_MS` |
| latency_count | 지연 측정 | count-based | 패턴별 | `BENCH_LAT_COUNT` |

### 3.2 패턴별 latency 기본값

| 패턴 | LAT_COUNT 기본값 | 제수(divisor) |
|------|------------------|---------------|
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

참고:
- Java `BenchGateway`는 현재 `1000/500` 불일치가 있어 `200/200` 정렬이 필요하다.

### 3.3 throughput 측정 규칙

1. throughput은 duration window 동안의 수신량으로 계산한다.
2. 공식식:
   `throughput = measure_recv / duration_seconds`
3. `warmup/settle/drain` 수신량은 throughput 계산에서 제외한다.
4. STREAM 계열은 inflight 제어(`BENCH_STREAM_INFLIGHT`)를 유지한다.

### 3.4 latency 측정 규칙

1. latency는 throughput과 분리된 count phase에서 측정한다.
2. RTT 패턴:
   `latency_us = elapsed_us / (lat_count * 2)`
3. PUBSUB:
   `latency_us = elapsed_us / received_count`
4. GATEWAY/SPOT:
   `latency_us = elapsed_us / lat_count`

---

## 4. Metric Definitions

### 4.1 필수 메트릭

| 메트릭 | 단위 | 계산 방식 |
|--------|------|-----------|
| throughput | msgs/sec | `measure_recv / duration_seconds` |
| latency | us | 패턴별 count phase 공식 적용 |

### 4.2 latency divisor 규칙

| 유형 | divisor | 패턴 |
|------|---------|------|
| 양방향 RTT | `lat_count * 2` | PAIR, DEALER_*, ROUTER_*, STREAM* |
| 단방향 pub-sub | `received_count` | PUBSUB |
| 단방향 멀티홉 | `lat_count` | GATEWAY, SPOT |

---

## 5. Statistics & Diff

### 5.1 집계 규칙

- 기본 집계: `median`
- 공식 기본값: `--runs 1`
- 노이즈 완화가 필요하면 `--runs 3` 이상 명시 실행

| 스위트 | runs 기본값 |
|--------|------------|
| benchwithzlink/single | 1 |
| benchwithzmq/single | 1 |
| bindings/bench | 1 |

### 5.2 diff 계산

```text
throughput_diff = ((current - base) / base) * 100.0
latency_diff    = ((base - current) / base) * 100.0
```

- `base <= 0`이면 diff는 `"N/A"`를 출력한다.

현행 불일치:
- `benchwithzlink/run_comparison.py`, `run_binding_comparison.py`는 일부 케이스에서 `0` 출력
- `benchwithzmq/single/run_comparison.py`는 `"N/A"` 출력

To-be:
- 전 스위트 `"N/A"`로 통일

---

## 6. Environment Variables

### 6.1 공통

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `BENCH_DEBUG` | 디버그 로그 | unset |
| `BENCH_IO_THREADS` | context I/O threads | 0(기본) |
| `BENCH_MSG_SIZES` | 테스트 size 목록 | `64,256,1024,65536,131072,262144` |
| `BENCH_TRANSPORTS` | 테스트 transport 목록 | 스위트 기본값 |
| `BENCH_TASKSET` | Linux CPU pinning | 0 |
| `BENCH_LAT_COUNT` | latency count override | 패턴별 기본값 |

### 6.2 duration 모델 전용 (To-be)

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `BENCH_SINGLE_WARMUP_SECONDS` | warmup duration | 3 |
| `BENCH_SINGLE_DURATION_SECONDS` | throughput 측정 duration | 5 |
| `BENCH_SINGLE_SETTLE_MS` | settle 시간(ms) | 300 |
| `BENCH_SINGLE_DRAIN_MS` | drain 시간(ms) | 300 |

### 6.3 Single/Bindings 전용

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `BENCH_SINGLE_TIMEOUT_SECONDS` | 프로세스 timeout | 120 |
| `BENCH_STREAM_TIMEOUT_MS` | STREAM socket timeout | 5000 |
| `BENCH_STREAM_DRAIN_TIMEOUT_MS` | STREAM drain timeout | `max(io*6,30000)` |
| `BENCH_STREAM_INFLIGHT` | STREAM inflight window | 10 |
| `BENCH_STREAM_HWM` | STREAM HWM | 100000 |
| `BENCH_STREAM_SERVER_IO_THREADS` | STREAM server io threads | 4 |
| `BENCH_STREAM_CLIENT_THREADS` | STREAM client worker threads | auto |
| `BENCH_MAX_SOCKETS` | context max sockets | auto |
| `BENCH_STD_CACHE_FILE` | std_zmq cache file | platform/arch suffix |
| `BENCH_LIBZMQ_LIB_DIR` | libzmq dir | auto-detected |
| `ZLINK_LIBRARY_PATH` | native lib path override | auto |

### 6.4 Legacy(count throughput) 호환 변수

| 변수 | 설명 | 상태 |
|------|------|------|
| `BENCH_MSG_COUNT` | throughput msg_count override | deprecated (duration 전환 후) |
| `BENCH_WARMUP_COUNT` | warmup count override | deprecated |

---

## 7. Message Size & Transport Matrix

### 7.1 표준 size

```text
[64, 256, 1024, 65536, 131072, 262144]
```

### 7.2 transport 매트릭스

| 스위트 | 패턴군 | transport |
|--------|--------|-----------|
| benchwithzlink/single | PAIR/PUBSUB/DEALER/ROUTER | tcp,inproc,ipc (Windows: tcp,inproc) |
| benchwithzlink/single | STREAM/GATEWAY/SPOT | tcp,tls,ws,wss |
| benchwithzmq/single | PAIR/PUBSUB/DEALER/ROUTER | tcp,inproc,ipc |
| benchwithzmq/single | STREAM(libzmq) | tcp |
| benchwithzmq/single | STREAM(zlink) | tcp,tls,ws,wss |
| bindings/bench | PAIR/PUBSUB/DEALER_*/ROUTER_* | tcp,inproc,ipc |
| bindings/bench | STREAM*/GATEWAY/SPOT | tcp,ws |

---

## 8. Pattern Matrix

| 패턴 | zlink/single | zmq/single | bindings |
|------|:------------:|:----------:|:--------:|
| PAIR | O | O | O |
| PUBSUB | O | O | O |
| DEALER_DEALER | O | O | O |
| DEALER_ROUTER | O | O | O |
| ROUTER_ROUTER | O | O | O |
| ROUTER_ROUTER_POLL | O | O | O |
| STREAM | O | O | O |
| STREAM_LEN32BE | — | — | O |
| GATEWAY | O | — | O |
| SPOT | O | — | O |

---

## 9. Cache Strategy

| 스위트 | 파일 |
|--------|------|
| benchwithzlink/single | `baseline_cache_<platform>-<arch>.json` |
| benchwithzmq/single | `std_zmq_single_cache_<platform>-<arch>.json` |
| bindings/bench | `zlink_cache_<platform>-<arch>.json` |

cache meta에 아래 필드를 포함하는 것을 권장한다.

- `measure_mode` (`hybrid_duration_throughput`)
- `duration_seconds`
- `lat_count_policy`
- `updated_at_utc`

---

## 10. Bindings Infra Rules

### 10.1 실행 흐름

`bindings/bench/common/run_benchmarks.sh` -> `run_binding_comparison.py` -> binding runner 호출

runner 인자 규격:

```text
<runner> <PATTERN> <transport> <size> <build_dir>
```

### 10.2 fallback 금지

- `--allow-core-fallback`은 디버그 전용
- 공식 벤치 채택/회귀 판단에는 사용 금지
- fallback hit 발생 시 해당 결과 invalid

---

## 11. 적용 가이드

### 11.1 benchwithzlink/single

- 목표: throughput duration phase 도입, latency는 기존 count 공식 유지
- 남은 항목:
  - throughput loop를 duration 기반으로 전환
  - `BENCH_SINGLE_DURATION_SECONDS`/warmup/drain 변수 연결
  - diff `base<=0 -> "N/A"` 정렬

### 11.2 benchwithzmq/single

- 목표: zlink/single과 동일 모델 정렬
- 남은 항목:
  - libzmq/zlink 양쪽 throughput duration 전환
  - phase 변수 연결 및 table 출력 동일성 검증

### 11.3 bindings/bench

- 목표: 모든 바인딩에서 throughput duration, latency count 통일
- 남은 항목:
  - 각 바인딩 러너 duration phase 구현 정렬
  - `run_binding_comparison.py` diff `"N/A"` 정렬
  - Java `BenchGateway` `WARMUP/LAT_COUNT=200/200` 정렬

---

## 12. Implementation Checklist

### Phase 1: 문서/정책

- [x] throughput(duration) + latency(count) 정책 명문화
- [x] phase/metric/diff/fallback 규칙 정리

### Phase 2: single core 전환

- [ ] benchwithzlink/single throughput duration 전환
- [ ] benchwithzmq/single throughput duration 전환
- [ ] phase env 변수 연결 (`BENCH_SINGLE_*`)

### Phase 3: bindings 전환

- [ ] python/node/dotnet/java/cpp throughput duration 정렬
- [ ] diff `"N/A"` 통일
- [ ] Java Gateway count 정렬

### Phase 4: 검증

- [ ] 패턴별 RESULT 완전성 검증
- [ ] cache meta(mode/duration) 저장 검증
- [ ] runs=1 기본 + runs=3 재현성 샘플 검증

---

## Appendix A: 계산 레퍼런스

```python
def throughput_msgs_per_sec(measure_recv: int, duration_seconds: int) -> float:
    return measure_recv / max(1, duration_seconds)

def latency_rtt_us(elapsed_ms: float, lat_count: int) -> float:
    return (elapsed_ms * 1000.0) / max(1, lat_count * 2)

def latency_oneway_us(elapsed_ms: float, denom: int) -> float:
    return (elapsed_ms * 1000.0) / max(1, denom)

def diff_pct(base: float, current: float, metric: str):
    if base <= 0:
        return "N/A"
    if metric == "throughput":
        return ((current - base) / base) * 100.0
    return ((base - current) / base) * 100.0
```

