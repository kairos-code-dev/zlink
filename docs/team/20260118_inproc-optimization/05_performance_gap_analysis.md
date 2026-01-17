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
