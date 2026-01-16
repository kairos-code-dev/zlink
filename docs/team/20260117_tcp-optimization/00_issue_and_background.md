# TCP Performance Optimization - Issue & Background

**Status:** Resolved in Phase 1 (see `02_fix_and_results.md`).

## 현재 이슈: TCP 성능 격차

### 문제 정의

**발견**: IPC는 libzmq-ref 수준 달성 (81-106%), TCP는 절반 수준 (53-58%)

**성능 비교 (10K messages, 64B)**:

| Pattern | libzmq-ref TCP | zlink TCP | 달성률 | 성능 격차 |
|---------|----------------|-----------|--------|----------|
| PAIR | 5.0 ~ 5.4 M/s | 2.7 ~ 2.9 M/s | **53-58%** | **-2.5 M/s** |
| DEALER_DEALER | 5.0 ~ 5.6 M/s | 2.8 ~ 2.9 M/s | **50-58%** | **-2.7 M/s** |
| ROUTER_ROUTER_POLL | 4.0 ~ 4.5 M/s | 2.2 M/s | **49-55%** | **-2.3 M/s** |

**평균**: zlink TCP는 libzmq-ref의 **약 절반** 성능

### Transport별 성능 달성률 비교

| Transport | zlink 성능 | libzmq-ref 성능 | 달성률 | 상태 |
|-----------|-----------|-----------------|--------|------|
| **IPC** | 4.78 M/s | 4.5 ~ 5.9 M/s | **81-106%** | ✅ **해결됨** |
| **TCP** | 2.9 M/s | 5.0 ~ 5.4 M/s | **53-58%** | ❌ **문제** |
| **inproc** | 4.6 M/s | N/A | N/A | ✅ 양호 |

### 왜 문제인가?

1. **일관성 부족**
   - IPC는 성공, TCP는 실패
   - 같은 Phase 5 최적화 적용됨
   - 근본 원인이 transport별로 다름

2. **Production 영향**
   - TCP는 네트워크 통신의 핵심
   - 성능이 절반이면 실용성 저하
   - libzmq-ref 대체제로서 부족

3. **기대치 미충족**
   - IPC 성공으로 기대감 상승
   - TCP도 동일 수준 가능해야 함
   - 현재 상태는 불완전

---

## IPC 최적화 작업 요약

### Phase 1-4: 실패한 접근들

#### Phase 1: Investigation
- 문제 발견: IPC deadlock at >2K messages
- 원인 분석: Race condition in backpressure handling

#### Phase 2: Double-Check Pattern
- 접근: `flush()` 후 pending buffers 재확인
- 결과: **70% success** @ 2K, **0%** @ 10K
- 평가: 부분적 개선, 근본 해결 실패

#### Phase 3: Partial Strand Serialization
- 접근: Read/Write 핸들러만 strand 직렬화
- 결과: **60% success** @ 2K (-10% 악화!)
- 평가: Strand overhead가 성능 저하 유발

#### Phase 4: Complete Strand Serialization
- 접근: 모든 핸들러 strand 직렬화 + dispatch → post
- 결과: **30% success** @ 2K (-40% 재앙!)
- 평가: Strand는 근본적으로 잘못된 접근

**Phase 1-4 교훈**:
- ❌ Strand serialization은 IPC 초고속 환경에 부적합
- ❌ 직렬화 오버헤드 > 경쟁 조건 방지 효과
- ❌ 증상 치료 vs 근본 원인 해결

### Phase 5: 성공한 해결책 ✅

#### 핵심 아이디어: Speculative Read

**개념**: Backpressure 해제 시점에 데이터가 이미 도착했는지 동기적으로 확인

**구현**:

1. **Transport 인터페이스 확장**
   ```cpp
   // i_asio_transport.hpp
   virtual std::size_t read_some(std::uint8_t *buffer, std::size_t len) = 0;
   virtual bool supports_speculative_write() const { return true; }
   ```

2. **Speculative Read 구현**
   ```cpp
   // asio_engine.cpp
   bool asio_engine_t::speculative_read() {
       if (_read_pending || _io_error || !_transport)
           return false;

       // Synchronous non-blocking read
       const std::size_t bytes = _transport->read_some(
           _read_buffer_ptr, read_size);

       if (bytes == 0) {
           if (errno == EAGAIN) return false; // No data yet
           error(connection_error);
           return true;
       }

       // Data found! Process immediately
       on_read_complete(boost::system::error_code(), bytes);
       return true;
   }
   ```

3. **Backpressure 해제 시 호출**
   ```cpp
   bool restart_input_internal() {
       // ... drain pending buffers ...

       _input_stopped = false;
       _session->flush();

       // CRITICAL: Speculative read
       speculative_read(); // ← THE KEY!

       return true;
   }
   ```

4. **IPC Speculative Write 비활성화**
   ```cpp
   // ipc_transport.cpp
   bool ipc_transport_t::supports_speculative_write() const {
       return ipc_allow_sync_write() && !ipc_force_async();
   }
   ```

#### 왜 성공했는가?

**Before (Phase 1-4)**:
```
session calls restart_input()
  ↓
_input_stopped = false
  ↓
session->flush()
  ↓
[Race Window: New data arrives]
  ↓
async_read_some() called
  ↓
Data already in kernel buffer → orphaned!
  → DEADLOCK
```

**After (Phase 5)**:
```
session calls restart_input()
  ↓
_input_stopped = false
  ↓
session->flush()
  ↓
speculative_read() ← Immediately checks kernel buffer
  ↓
IF data ready:
    read_some() returns bytes
    on_read_complete() processes
    → No orphan possible!
ELSE:
    EAGAIN → async_read_some() continues
    → Normal async flow
```

**핵심**: Race window 제거 + Zero latency for ready data

### Phase 5 성과

#### 안정성: 100% 해결 ✅

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| 2K messages | 70% → 60% → 30% | **100%** | **+70%p** |
| 10K messages | 0% | **100%** | **+100%p** |
| 200K messages | Not tested | **100%** | **완전 해결** |
| Deadlock count | Frequent | **Zero** | **완전 제거** |

#### 성능: libzmq-ref 수준 달성 ✅

**IPC Benchmarks (10K messages, 64B)**:

| Pattern | zlink Phase 5 | libzmq-ref | 달성률 |
|---------|--------------|------------|--------|
| DEALER_DEALER | 4.91 M/s | 4.5 ~ 5.9 M/s | **83-109%** |
| PAIR | 4.78 M/s | 4.5 ~ 5.9 M/s | **81-106%** |
| DEALER_ROUTER | 4.56 M/s | - | - |
| PUBSUB | 4.55 M/s | - | - |
| ROUTER_ROUTER | 3.65 M/s | - | - |
| ROUTER_ROUTER_POLL | 3.35 M/s | ~3.5 M/s | **~96%** |

**200K messages (PAIR/ipc/64B)**: 4.77 M/s ✅

#### 포괄성: 36/36 테스트 통과 ✅

**Test Matrix**: 6 patterns × 3 transports × 2 message sizes

| Transport | 64B Success | 1KB Success | Total |
|-----------|-------------|-------------|-------|
| tcp | 6/6 | 6/6 | 12/12 |
| ipc | 6/6 | 6/6 | 12/12 |
| inproc | 6/6 | 6/6 | 12/12 |
| **Total** | **18/18** | **18/18** | **36/36** ✅ |

### IPC 최적화 핵심 교훈

#### ✅ 성공 요인

1. **근본 원인 해결**
   - 증상 치료 (double-check) → 실패
   - Race window 제거 (speculative read) → 성공

2. **Transport 특성 고려**
   - IPC는 초고속, 초저지연
   - Speculative read는 EAGAIN 안전
   - Async write 강제로 안정성 확보

3. **측정 기반 접근**
   - Phase별 성능 측정
   - 70% → 60% → 30% → 100%
   - 데이터가 결정 가이드

4. **팀 협업**
   - Claude, Codex, Gemini 다각도 분석
   - 실패한 접근도 학습 자료

#### ❌ 실패 요인

1. **Strand Anti-Pattern**
   - 직렬화 ≠ 성능 향상 보장
   - IPC 환경에서 오버헤드 > 이득
   - 측정 없이 가정하면 실패

2. **부분적 해결책**
   - Double-check는 확률적 개선
   - 100% 해결에는 알고리즘 변경 필요

3. **가정의 오류**
   - "완전 직렬화 = 안전" → 틀림
   - "post > dispatch" → 틀림
   - 항상 측정으로 검증

### Phase 5 핵심 변경 파일

**인터페이스**:
- `src/asio/i_asio_transport.hpp`: `read_some()`, `supports_speculative_write()` 추가

**Engine**:
- `src/asio/asio_engine.cpp`: `speculative_read()`, `restart_input_internal()` 구현
- `src/asio/asio_engine.hpp`: 선언 추가

**Transport 구현**:
- `src/asio/tcp_transport.{cpp,hpp}`: `read_some()` 구현
- `src/asio/ipc_transport.{cpp,hpp}`: `read_some()`, `supports_speculative_write()` 구현
- `src/asio/ssl_transport.{cpp,hpp}`: `read_some()` 구현
- `src/asio/ws_transport.{cpp,hpp}`: `read_some()` 구현
- `src/asio/wss_transport.{cpp,hpp}`: `read_some()` 구현

**문서**:
- `CLAUDE.md`: Performance Notes 업데이트
- `docs/team/20260116_ipc-deadlock-debug/`: 완전한 분석 trail

---

## TCP 최적화의 맥락

### IPC 성공이 시사하는 것

1. **Phase 5 기법의 효과**
   - Speculative read는 **모든 transport**에 적용됨
   - IPC에서 효과 입증됨
   - 그런데 왜 TCP는 절반 성능?

2. **Transport별 최적화 필요성**
   - IPC: Async write 비활성화 필요
   - TCP: 추가 최적화 필요?
   - One-size-fits-all은 작동 안 함

3. **libzmq-ref 수준 달성 가능성**
   - IPC가 증명: 가능함
   - 같은 프레임워크 (ASIO)
   - TCP도 가능해야 함

### TCP 최적화 목표

**성능 목표**:
| Pattern | 현재 | 목표 | 증가율 |
|---------|------|------|--------|
| PAIR/tcp/64B | 2.9 M/s | **5.4 M/s** | **+86%** |
| DEALER_DEALER/tcp/64B | 2.9 M/s | **5.6 M/s** | **+93%** |
| ROUTER_ROUTER_POLL/tcp/64B | 2.2 M/s | **4.5 M/s** | **+105%** |

**성공 기준**:
- ✅ libzmq-ref 대비 **80%+** 성능 달성
- ✅ 안정성 유지 (100% success rate)
- ✅ 모든 패턴에서 일관된 향상

### 접근 전략

**IPC Phase 5에서 배운 점 적용**:

1. **측정 먼저**
   - TCP Speculative write 통계 수집
   - 실제로 작동하는가?
   - EAGAIN 비율은?

2. **근본 원인 찾기**
   - ASIO 오버헤드?
   - Socket options?
   - Write 패턴?

3. **Transport 특성 고려**
   - TCP는 네트워크 스택
   - IPC와 다른 최적화 필요
   - Batching? Corking?

4. **단계적 접근**
   - Phase by phase
   - 각 단계 측정
   - 실패도 기록

**피해야 할 것**:
- ❌ 가정 기반 최적화
- ❌ 측정 없는 변경
- ❌ All-or-nothing 접근

---

## 요약

### 현재 상황

**IPC**: ✅ **완전 해결**
- Phase 5로 100% 안정성 달성
- libzmq-ref 81-106% 성능
- Speculative read pattern 성공

**TCP**: ❌ **성능 격차**
- 안정성은 100%
- 성능은 libzmq-ref의 53-58%
- 2배 향상 필요

### 작업 방향

**Phase 5 성공 경험 활용**:
1. 측정 기반 접근
2. 근본 원인 규명
3. Transport 특성 고려
4. 단계적 최적화

**목표**:
- TCP 성능 2배 향상
- libzmq-ref 80%+ 달성
- zlink를 진정한 libzmq-ref 대체제로 완성

**다음 단계**:
1. TCP Speculative write 통계 수집
2. Socket options 비교
3. libzmq-ref TCP 구현 분석
4. 최적화 구현 및 벤치마크

---

**문서 버전**: 1.0
**작성일**: 2026-01-17
**작성자**: Claude Code
**참조**:
- `docs/team/20260116_ipc-deadlock-debug/FINAL_SUMMARY.md`
- `docs/team/20260116_ipc-deadlock-debug/final_benchmark_results.md`
- `docs/team/20260116_ipc-deadlock-debug/tcp_performance_gap_analysis.md`
