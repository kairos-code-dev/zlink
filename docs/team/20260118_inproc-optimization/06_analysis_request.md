# inproc 추가 최적화 요청

## 현황 요약

### 성능 Gap (5회 평균, 10K messages, 64B)

| Pattern | zlink | libzmq-ref | Gap | 달성률 |
|---------|-------|------------|-----|--------|
| PAIR | 4.83 M/s | 6.04 M/s | -1.21 M/s | **80.0%** |
| PUBSUB | 4.66 M/s | 5.50 M/s | -0.84 M/s | **84.7%** |
| DEALER_DEALER | 4.86 M/s | 5.97 M/s | -1.11 M/s | **81.5%** |
| DEALER_ROUTER | 4.53 M/s | 5.34 M/s | -0.81 M/s | **84.9%** |

**평균: 82.8%** (TCP 93%보다 10%p 낮음)

## Phase 1 최적화 (Codex 완료)

**변경 사항**:
1. ✅ condition_variable → signaler 기반 wakeup
2. ✅ Lock-free recv path (single-reader)
3. ✅ _active 상태 추적
4. ✅ schedule_if_needed() 단순화

**효과**: libzmq-ref 구조와 거의 동일하게 변경됨

**하지만**: 여전히 ~17% gap 존재

## 추가 분석 필요 영역

### 1. ASIO Integration Overhead

**현재 구조**:
```cpp
// mailbox.cpp
void mailbox_t::schedule_if_needed() {
    if (!_scheduled.exchange(true, std::memory_order_acquire)) {
        if (_pre_post)
            _pre_post(_handler_arg);
        _io_context->post([this]() { _handler(_handler_arg); });
    }
}
```

**의문점**:
- `io_context->post()` 오버헤드가 inproc에서 병목?
- `_scheduled` atomic exchange가 성능에 영향?
- Handler 호출 빈도가 너무 높은가?

**libzmq-ref**:
- epoll 직접 사용
- signaler fd를 epoll에 등록
- 최소한의 추상화

**Gap 원인?**: ASIO event loop overhead

### 2. ypipe Implementation

**확인 필요**:
- `src/ypipe.hpp` 구현이 libzmq-ref와 100% 동일한가?
- Atomic ordering (memory_order_relaxed vs seq_cst)
- Cache line padding
- False sharing 방지

**가능성**: Subtle한 구현 차이가 성능에 영향

### 3. Command Pipe (cpipe) Behavior

**send path**:
```cpp
void mailbox_t::send(const command_t &cmd_) {
    _sync.lock();
    _cpipe.write(cmd_, false);
    const bool ok = _cpipe.flush();
    if (!ok) {
        _signaler.send();
        // ...
    }
    _sync.unlock();

    if (!ok)
        schedule_if_needed();
}
```

**의문점**:
- `_sync.lock()` 필요한가? (single-writer?)
- `flush()` 실패 조건은?
- `schedule_if_needed()` 호출 빈도?

### 4. Memory Ordering / Atomics

**critical path**:
- `_scheduled` (atomic bool)
- ypipe 내부 atomics
- cpipe flush/check_read

**가능성**:
- seq_cst가 relaxed로 가능한 곳?
- Memory barrier overhead?
- Cache coherence traffic?

## 분석 방법 (WSL에서 가능한 것)

### 1. Code Comparison

**libzmq-ref vs zlink**:
```bash
# libzmq-ref mailbox 구현
cat /path/to/libzmq/src/mailbox.cpp

# zlink mailbox 구현
cat src/mailbox.cpp

# 차이점 비교
diff -u /path/to/libzmq/src/mailbox.cpp src/mailbox.cpp
```

### 2. Micro-benchmark

**ASIO overhead 측정**:
```cpp
// 간단한 벤치마크
for (int i = 0; i < 1000000; i++) {
    io_context.post([](){});
}
io_context.run();
```

**목표**: `post()` 호출 1M회 시간 측정

### 3. Message Count vs Throughput

**다양한 메시지 수 테스트**:
```bash
for count in 1000 5000 10000 50000 100000; do
    BENCH_MSG_COUNT=$count ./build/bin/comp_zlink_pair zlink inproc 64
    BENCH_MSG_COUNT=$count ./build/bin/comp_std_zmq_pair std inproc 64
done
```

**목표**: Scalability 패턴 비교

### 4. Signaler Frequency

**통계 추가**:
```cpp
// mailbox.cpp
static std::atomic<uint64_t> signaler_send_count(0);
static std::atomic<uint64_t> schedule_count(0);

void send(...) {
    // ...
    if (!ok) {
        ++signaler_send_count;
        _signaler.send();
    }
    if (!ok)
        ++schedule_count;
        schedule_if_needed();
}
```

**목표**: Signaling 빈도 측정

## 예상 최적화 옵션

### Option A: ASIO Bypass (Aggressive)

**아이디어**: inproc만 ASIO 없이 직접 signaler 사용

**구현**:
```cpp
// mailbox.hpp
#ifdef ZMQ_ASIO_INPROC_BYPASS
    bool _bypass_asio;
#endif

// recv() in bypass mode
if (_bypass_asio) {
    // 직접 signaler.wait() + cpipe.read()
} else {
    // 기존 ASIO 경로
}
```

**예상 효과**: +0.5-0.8 M/s (10-15%)

### Option B: Batch Notification

**아이디어**: 여러 메시지를 한 번에 notify

**구현**:
```cpp
void send(...) {
    _cpipe.write(cmd_, false);
    if (++_pending_count >= BATCH_SIZE) {
        _cpipe.flush();
        _signaler.send();
        _pending_count = 0;
    }
}
```

**예상 효과**: +0.2-0.4 M/s (latency 증가 tradeoff)

### Option C: Atomic Optimization

**아이디어**: memory_order 최적화

**구현**:
```cpp
// _scheduled
_scheduled.exchange(true, std::memory_order_acquire);
→ _scheduled.exchange(true, std::memory_order_relaxed);

// ypipe atomics
std::memory_order_seq_cst
→ std::memory_order_release/acquire
```

**예상 효과**: +0.1-0.3 M/s (subtle)

### Option D: Lock Removal

**아이디어**: send path의 _sync.lock() 제거

**전제**: single-writer assumption 검증

**예상 효과**: +0.1-0.2 M/s

## 요청사항

### Codex에게

1. **libzmq-ref inproc 구현 분석**
   - mailbox, ypipe, signaler 상세 비교
   - 차이점 목록화
   - 성능 영향 평가

2. **ASIO overhead 측정**
   - `io_context.post()` 벤치마크
   - Handler 호출 빈도 프로파일
   - Bypass 가능성 검토

3. **최적화 제안**
   - 우선순위별 옵션
   - 예상 개선폭
   - 구현 난이도

### Gemini에게

1. **Memory ordering 분석**
   - Atomic operations review
   - Memory barrier 필요성
   - Relaxed ordering 가능 여부

2. **Cache 최적화**
   - False sharing 검사
   - Cache line padding 제안
   - Prefetch 기회

3. **Alternative 접근**
   - Lock-free 개선
   - Wait-free 가능성
   - Hybrid 전략

## 목표

**inproc 성능 목표**: 90%+ 달성 (현재 83% → 목표 90%)

**예상 최종 성능**:
| Pattern | 현재 | 목표 | libzmq | 달성률 |
|---------|------|------|--------|--------|
| PAIR | 4.83 M/s | **5.43 M/s** | 6.04 M/s | **90%** |
| DEALER_DEALER | 4.86 M/s | **5.37 M/s** | 5.97 M/s | **90%** |

**Impact**: 모든 transport 90%+ → zlink = production ready

---

**Priority**: High
**Estimated Effort**: 1-2 weeks
**Dependency**: Code analysis + profiling
