# inproc Performance Gap Analysis

## 현재 상황

### 최신 10회 평균 (Phase 18, 10K messages, 64B)

| Pattern | zlink | libzmq-ref | Gap | 달성률 |
|---------|-------|------------|-----|--------|
| PAIR | 5.95 M/s | 6.05 M/s | **-0.10 M/s** | **98.4%** |
| PUBSUB | 5.64 M/s | 5.42 M/s | **+0.22 M/s** | **104.0%** |
| DEALER_DEALER | 5.85 M/s | 6.08 M/s | **-0.23 M/s** | **96.2%** |
| DEALER_ROUTER | 4.92 M/s | 5.25 M/s | **-0.33 M/s** | **93.7%** |
| ROUTER_ROUTER | 4.49 M/s | 4.85 M/s | **-0.37 M/s** | **92.4%** |
| ROUTER_ROUTER_POLL | 4.12 M/s | 4.07 M/s | **+0.05 M/s** | **101.2%** |

**평균 달성률: ~97.6%** ✅  
**전 패턴 90%+ 유지**

> Note: Phase 18은 ROUTER mandatory 경로에서 `check_hwm()` 중복 호출을 제거한 결과임.
> Phase 17은 zlink 벤치에 `bench_send_fast/bench_recv_fast`를 확대 적용한 결과임.
> Phase 15는 10회 평균으로 안정성 확인 결과임.

## 메시지 사이즈별 성능 (Phase 20, inproc)

### 3회 평균 요약

| Size | msg_count | Avg 달성률 |
|------|-----------|------------|
| 64 | 10,000 | **96.3%** |
| 256 | 10,000 | **97.6%** |
| 1024 | 10,000 | **96.7%** |
| 65536 | 2,000 | **105.7%** |
| 131072 | 2,000 | **107.8%** |
| 262144 | 2,000 | **90.8%** |

### 하락 구간 (90% 미만)

- size=1024: DEALER_ROUTER 76.6%
- size=65536: DEALER_DEALER 71.9%
- size=131072: PUBSUB 81.7%, DEALER_DEALER 79.9%, DEALER_ROUTER 81.1%, ROUTER_ROUTER 86.5%
- size=262144: PUBSUB 72.7%, ROUTER_ROUTER_POLL 80.9%

> Note: size>1024 구간은 msg_count=2,000으로 측정해 변동성 영향 가능.

### 저하 패턴 재확인 (Phase 21, 5-run avg)

| Size | Pattern | zlink | libzmq-ref | 달성률 |
|------|---------|-------|------------|--------|
| 1024 | DEALER_ROUTER | 1.98 M/s | 2.61 M/s | **76.2%** |
| 65536 | DEALER_DEALER | 0.15 M/s | 0.21 M/s | **69.1%** |
| 131072 | DEALER_DEALER | 0.09 M/s | 0.15 M/s | **60.7%** |
| 131072 | ROUTER_ROUTER | 0.10 M/s | 0.12 M/s | **82.8%** |
| 262144 | PUBSUB | 0.05 M/s | 0.07 M/s | **72.3%** |

> Note: PUBSUB(131072)과 DEALER_ROUTER(131072), ROUTER_ROUTER_POLL(262144)은
> 5회 평균에서 90%+로 회복.

### atomic_counter intrinsics 실험 (Phase 22)

- large-size 일부 개선(ROUTER_ROUTER 128K 95.1%, DEALER_DEALER 64K 78.6%),
  하지만 64B DEALER_ROUTER 87.9%로 회귀 → 롤백.

### msg_count 민감도 (Phase 23)

- size=131072 DEALER_DEALER는 msg_count 500/1000/2000 모두 60~78%로 유지.
- low ratio가 msg_count 편차만으로 설명되기 어렵다고 판단.

### syscall 비교 (Phase 24)

- strace -c 기준 zlink는 read(EAGAIN) 비중이 높고 libzmq는 poll 비중이 높음.
- 총 syscall 규모는 유사하여 user-space 경로 차이 가능성 유지.

### non-blocking wait(0) 복귀 실험 (Phase 25)

- `wait(0)` 복귀 후 저하 패턴 개선 없음, 일부 악화.
- `recv_failable` 경로 유지 결정.

### perf 설치 상태 (Phase 26)

- WSL 커널용 perf 미설치, sudo 필요로 설치 불가.

### strace -f 비교 (Phase 27)

- 저하 패턴 전반에서 zlink는 futex + read(EAGAIN) 비중이 높음.
- libzmq는 epoll_wait/poll 비중이 높고 read 비중은 낮음.
- syscall 분포 차이는 관찰되나 user-space 경로 차이 가능성 정도로만 판단.

### HWM 정렬 (Phase 28)

- libzmq DEALER_ROUTER 벤치에 HWM=0을 적용해 zlink와 정렬.
- size=1024에서 DEALER_ROUTER 100.9%로 회복, 131072에서도 97.9%.

### Large-size refresh (Phase 29, inproc, 5-run avg)

| Size | Pattern | libzmq-ref | zlink | 달성률 |
|------|---------|------------|-------|--------|
| 65536 | DEALER_DEALER | 0.16 M/s | 0.12 M/s | **71.7%** |
| 131072 | DEALER_DEALER | 0.11 M/s | 0.08 M/s | **75.7%** |
| 262144 | DEALER_DEALER | 0.07 M/s | 0.06 M/s | **84.6%** |
| 65536 | PUBSUB | 0.16 M/s | 0.11 M/s | **70.1%** |
| 131072 | PUBSUB | 0.11 M/s | 0.09 M/s | **76.4%** |
| 262144 | PUBSUB | 0.07 M/s | 0.06 M/s | **85.1%** |
| 65536 | ROUTER_ROUTER | 0.16 M/s | 0.11 M/s | **69.6%** |
| 131072 | ROUTER_ROUTER | 0.11 M/s | 0.08 M/s | **74.5%** |
| 262144 | ROUTER_ROUTER | 0.07 M/s | 0.05 M/s | **77.9%** |

> Note: `BENCH_TRANSPORTS=inproc`, `BENCH_MSG_SIZES=65536,131072,262144`,
> `--runs 5`, `--refresh-libzmq` 기준.

### ROUTER_ROUTER_POLL large-size timeout (Phase 30)

- run_comparison에서 size=262144가 timeout으로 중단됨.
- msg_count=2000에서는 정상 완료:
  - zlink throughput 129,638.30 / latency 9.04 us
  - libzmq throughput 55,819.99 / latency 7.30 us
- large-size poll 벤치는 msg_count 조정 또는 큐 압력 원인 분석 필요.

### content_t padding 정렬 실험 (Phase 31)

- `msg_t::content_t` padding 추가로 data 16B 정렬 시도.
- 64K/128K/256K 전 패턴에서 throughput이 2~4%p 추가 하락 → 롤백.

### -march=native 빌드 플래그 영향 (Phase 32)

- `-O3 -march=native` 별도 빌드 시 large-size gap 대부분 해소.
- DEALER_DEALER/PUBSUB는 64K~256K 구간에서 +1~9%로 전환.
- ROUTER_ROUTER는 64K/128K 개선, 256K는 여전히 -5.7%.
- 코드 변경보다 컴파일 플래그/ISA 최적화 영향이 큼.

### native 최적화 옵션 추가 (Phase 33)

- `ENABLE_NATIVE_OPTIMIZATIONS=ON`으로 `-march=native`를 Release에 적용 가능.
- 기본값 OFF로 호환성 유지, 벤치/로컬 튜닝 시 활용.

### native 최적화 full sweep (Phase 34)

- `-march=native` + inproc 6 sizes 기준 대부분 90%+ 달성.
- ROUTER_ROUTER_POLL은 msg_count=2000 조건에서 small-size가 왜곡됨.

### ROUTER_ROUTER_POLL small-size 재측정 (Phase 35)

- default msg_count 기준 64B/256B/1024B 모두 90%+ 확인.
- large-size는 msg_count=2000 결과 유지.

### -mtune=native 효과 (Phase 36)

- ISA 확장 없이도 large-size 격차 대부분 해소.
- DEALER_DEALER/PUBSUB 64K~256K는 +0~9% 범위.
- ROUTER_ROUTER 256K는 -6.65% (93% 수준).

### 최신 5회 평균 (Phase 9 이후, 10K messages, 64B)

| Pattern | zlink | libzmq-ref | Gap | 달성률 |
|---------|-------|------------|-----|--------|
| PAIR | 5.84 M/s | 6.07 M/s | **-0.23 M/s** | **96.1%** |
| PUBSUB | 5.55 M/s | 5.32 M/s | **+0.23 M/s** | **104.4%** |
| DEALER_DEALER | 5.94 M/s | 6.07 M/s | **-0.13 M/s** | **97.9%** |
| DEALER_ROUTER | 5.45 M/s | 5.40 M/s | **+0.05 M/s** | **100.9%** |

**평균 달성률: ~99.8%** ✅

> Note: Phase 9는 벤치마크 핫패스(getenv) 오버헤드 제거로 측정값이 개선됨.

### ROUTER 패턴 재측정 (Phase 11 이후, 10K messages, 64B)

| Pattern | zlink | libzmq-ref | Gap | 달성률 |
|---------|-------|------------|-----|--------|
| ROUTER_ROUTER | 4.39 M/s | 4.69 M/s | **-0.30 M/s** | **93.6%** |
| ROUTER_ROUTER_POLL | 3.90 M/s | 3.83 M/s | **+0.07 M/s** | **101.9%** |

**ROUTER_ROUTER_POLL 갭 해소**  
→ ZMQ_FD 경로에서 추가 signaler 제거 효과로 판단

### 최신 5회 평균 (Phase 7 이후, 10K messages, 64B)

| Pattern | zlink | libzmq-ref | Gap | 달성률 |
|---------|-------|------------|-----|--------|
| PAIR | 5.17 M/s | 5.95 M/s | **-0.78 M/s** | **86.8%** |
| PUBSUB | 4.83 M/s | 5.56 M/s | **-0.73 M/s** | **86.9%** |
| DEALER_DEALER | 5.20 M/s | 5.96 M/s | **-0.76 M/s** | **87.2%** |
| DEALER_ROUTER | 4.85 M/s | 5.25 M/s | **-0.40 M/s** | **92.4%** |

**평균 달성률: ~88.3%** (목표 90%+ 미달)

### 5회 평균 성능 비교 (10K messages, 64B)

| Pattern | zlink | libzmq-ref | Gap | 달성률 |
|---------|-------|------------|-----|--------|
| PAIR | 4.83 M/s | 6.04 M/s | **-1.21 M/s** | **80.0%** ❌ |
| PUBSUB | 4.66 M/s | 5.50 M/s | **-0.84 M/s** | **84.7%** ❌ |
| DEALER_DEALER | 4.86 M/s | 5.97 M/s | **-1.11 M/s** | **81.5%** ❌ |
| DEALER_ROUTER | 4.53 M/s | 5.34 M/s | **-0.81 M/s** | **84.9%** ❌ |

**평균 달성률: ~82.8%** (TCP 93%보다 약 10%p 낮음)

### Transport별 달성률 비교

| Transport | 달성률 | Gap | 상태 |
|-----------|--------|-----|------|
| **IPC** | **81-106%** | 0 ~ +0.3 M/s | ✅ 해결 |
| **TCP** | **93%** | -0.4 M/s | ✅ 해결 |
| **inproc** | **83%** | -1.0 M/s | ❌ **개선 필요** |

## 문제 분석

### Phase 1 최적화 효과

Codex가 적용한 mailbox_t 최적화:
- condition_variable → signaler 기반 wakeup
- Lock-free recv path (single-reader)
- ASIO schedule 단순화

**결과**: 일부 개선되었으나 여전히 ~17% gap 존재

### 왜 inproc만 gap이 클까?

#### Hypothesis A: inproc 특성

**inproc 구조**:
```
Writer Thread                 Reader Thread
  ↓                               ↓
ypipe (lock-free queue)
  ↓                               ↓
mailbox (signaler)
  ↓                               ↓
ASIO event notification
```

**특징**:
- No network stack (pure memory)
- Lock-free ypipe for data
- mailbox for activation signals
- Cross-thread communication overhead

**문제점**:
- ASIO 추상화 레이어 오버헤드가 순수 메모리 통신에서 더 두드러짐
- IPC/TCP는 네트워크 I/O가 dominant → ASIO 오버헤드 상대적으로 작음
- inproc는 메모리 속도 → ASIO 오버헤드가 상대적으로 큼

#### Hypothesis B: signaler 구현 차이

**libzmq-ref signaler**:
- eventfd (Linux)
- socketpair (fallback)
- 최소한의 syscall

**zlink signaler**:
- 동일한 구현 사용?
- ASIO integration 추가 레이어?

**확인 필요**: `src/signaler.cpp` 분석

#### Hypothesis C: ypipe vs ASIO 상호작용

**libzmq-ref**:
```
ypipe.write() → mailbox.send() → eventfd write → epoll wakeup
```

**zlink**:
```
ypipe.write() → mailbox.send() → signaler → ASIO schedule → io_context wakeup
```

**추가 레이어**:
- ASIO의 `post()` / `dispatch()`
- io_context event queue
- Handler 호출 오버헤드

**Gap 원인?**: ASIO 레이어가 inproc 초고속에서 병목

#### Hypothesis D: Memory ordering / Cache coherence

**inproc 특성**:
- 극도로 빠른 cross-thread communication
- Cache line bouncing 민감
- Memory ordering 중요

**가능성**:
- ypipe의 atomic operations
- mailbox signaling
- ASIO handler dispatch

**Profiling 필요**: perf로 cache misses 측정

#### Hypothesis E: Batch processing 부족

**libzmq-ref**:
- recv 시 가능한 많은 메시지 한 번에 처리?
- Batch notification?

**zlink**:
- 메시지당 notification?
- ASIO handler 호출 빈도?

**확인 필요**: recv path 분석

## 조사 계획

### P0: Profiling (Immediate)

1. **perf stat 비교**
   ```bash
   # zlink inproc
   perf stat -e cycles,instructions,cache-misses,cache-references \
     BENCH_MSG_COUNT=10000 ./build/bin/comp_zlink_pair zlink inproc 64

   # libzmq-ref inproc
   perf stat -e cycles,instructions,cache-misses,cache-references \
     BENCH_MSG_COUNT=10000 ./build/bin/comp_std_zmq_pair std inproc 64
   ```

   **목표**: CPU cycles, IPC, cache miss rate 비교

2. **perf record + flamegraph**
   ```bash
   perf record -F 999 -g -- \
     BENCH_MSG_COUNT=100000 ./build/bin/comp_zlink_pair zlink inproc 64
   perf script | stackcollapse-perf.pl | flamegraph.pl > zlink_inproc.svg
   ```

   **목표**: Hotspot 식별

3. **strace syscall 비교**
   ```bash
   strace -c BENCH_MSG_COUNT=10000 ./build/bin/comp_zlink_pair zlink inproc 64
   strace -c BENCH_MSG_COUNT=10000 ./build/bin/comp_std_zmq_pair std inproc 64
   ```

   **목표**: Syscall 빈도 비교

### P1: Code Analysis (High)

1. **signaler 구현 비교**
   - `src/signaler.cpp` vs libzmq-ref
   - eventfd vs ASIO integration

2. **mailbox recv path 분석**
   - Lock-free인가?
   - Batch processing 여부
   - ASIO schedule 타이밍

3. **ypipe 구현 검증**
   - libzmq-ref와 동일한가?
   - Atomic ordering 올바른가?

### P2: Experimental Optimizations (Medium)

1. **ASIO bypass 실험**
   - inproc만 ASIO 없이 직접 signaler 사용
   - Baseline 성능 측정

2. **Batch notification**
   - 여러 메시지 한 번에 notify
   - Latency vs throughput tradeoff

3. **Cache line padding**
   - ypipe의 critical data structures
   - False sharing 제거

## 예상 개선 가능성

**현재 gap**: 약 1.0 M/s (17%)

**최적화 시나리오**:

| Optimization | 예상 개선 | 근거 |
|--------------|----------|------|
| ASIO overhead 제거 | +0.3-0.5 M/s | TCP/IPC에서 ASIO 효과 봄 |
| Batch processing | +0.2-0.3 M/s | Notification 빈도 감소 |
| Cache optimization | +0.1-0.2 M/s | Memory-intensive workload |
| **Total** | **+0.6-1.0 M/s** | **90-95% 달성 가능** |

**목표 달성률**: 90%+ (libzmq-ref의 5.4-5.8 M/s)

## 다음 단계

1. ✅ **Gap 정량화** - 완료 (82.8% 달성)
2. ⏳ **Profiling 실행** - perf stat, flamegraph
3. ⏳ **Code 분석** - signaler, mailbox, ypipe
4. ⏳ **최적화 구현** - 우선순위별
5. ⏳ **Benchmark 검증** - 목표 90%+

## 기대 효과

**inproc를 90%+로 개선 시**:

| Pattern | 현재 | 목표 | 개선폭 |
|---------|------|------|--------|
| PAIR | 4.83 M/s | **5.43 M/s** | **+0.6 M/s** |
| DEALER_DEALER | 4.86 M/s | **5.37 M/s** | **+0.5 M/s** |
| PUBSUB | 4.66 M/s | **4.95 M/s** | **+0.3 M/s** |

**Impact**:
- 모든 transport 90%+ 달성
- zlink = true libzmq-ref replacement
- Production ready 완성

---

**Status**: Gap identified, profiling needed
**Priority**: High (마지막 남은 최적화 기회)
**Timeline**: 1-2 weeks (profiling + optimization)
