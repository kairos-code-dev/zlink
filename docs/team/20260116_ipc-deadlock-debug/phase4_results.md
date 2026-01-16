# Phase 4 구현 결과 (Complete Strand 직렬화)

## 구현된 수정사항

### Phase 4: 미완성 Handler 래핑 완료

**Phase 3에서 누락된 부분 보완:**

1. **`asio_engine.cpp` - Timer 핸들러 래핑 (line 1360-1366)**
   ```cpp
   _timer->async_wait(boost::asio::bind_executor(
       *_strand, [this, id_](const boost::system::error_code &ec) {
           on_timer(id_, ec);
       }));
   ```

2. **`asio_engine.cpp` - Transport handshake 핸들러 래핑 (line 241-247)**
   ```cpp
   _transport->async_handshake(
       handshake_type,
       boost::asio::bind_executor(
           *_strand, [this](const boost::system::error_code &ec, std::size_t) {
               on_transport_handshake(ec);
           }));
   ```

3. **`asio_engine.cpp` - `restart_input()` dispatch → post 변경 (line 908)**
   ```cpp
   // Before (Phase 3):
   boost::asio::dispatch(*_strand, [this]() { restart_input_internal(); });

   // After (Phase 4):
   boost::asio::post(*_strand, [this]() { restart_input_internal(); });
   ```

**목표:** 모든 비동기 핸들러를 Strand에 태워 "완전한 직렬화" 달성

## 테스트 결과

### 2K 메시지 10회 반복 테스트

| Run | 결과 | Throughput (M/s) |
|-----|------|------------------|
| 1 | SUCCESS | 2.71 |
| 2 | **FAIL** (timeout) | - |
| 3 | **FAIL** (timeout) | - |
| 4 | **FAIL** (timeout) | - |
| 5 | **FAIL** (timeout) | - |
| 6 | **FAIL** (timeout) | - |
| 7 | SUCCESS | 2.77 |
| 8 | **FAIL** (timeout) | - |
| 9 | **FAIL** (timeout) | - |
| 10 | SUCCESS | 2.78 |

**성공률: 30% (10회 중 3회)** ⚠️⚠️⚠️

### 10K 메시지 5회 반복 테스트

| Run | 결과 |
|-----|------|
| 1 | **FAIL** (timeout) |
| 2 | **FAIL** (timeout) |
| 3 | **FAIL** (timeout) |
| 4 | **FAIL** (timeout) |
| 5 | **FAIL** (timeout) |

**성공률: 0% (5회 중 0회)**

## 결과 비교

| Phase | 2K 성공률 | 10K 성공률 | 변화 |
|-------|----------|-----------|------|
| Phase 2a (Double-check 전) | 70% | 0% | Baseline |
| Phase 2b (flush 후 check) | 70% | 미테스트 | 변화 없음 |
| Phase 3 (Partial Strand) | 60% | 0% | **-10%** 악화 |
| **Phase 4 (Complete Strand)** | **30%** | **0%** | **-40%** 재앙 🔥 |

## 핵심 발견

### 예상과 정반대의 결과

**Codex와 Gemini의 예상:**
- "Partial Strand가 문제였다"
- "완전한 직렬화로 100% 성공률 달성 가능"
- "Timer/Handshake 래핑 + post 변경으로 해결"

**실제 결과:**
- Phase 3 (부분 직렬화): **60%**
- Phase 4 (완전 직렬화): **30%** ← 절반으로 악화!

### 중대한 사실

**Strand 직렬화가 문제를 악화시킴:**
1. **Phase 2a (Strand 없음)**: 70% 성공
2. **Phase 3 (부분 Strand)**: 60% 성공 (-10%)
3. **Phase 4 (완전 Strand)**: 30% 성공 (-40%)

**패턴:** Strand 직렬화를 더 강화할수록 성능이 더 악화됨!

## 가능한 원인 분석

### Hypothesis A: Strand Serialization이 IPC에 부적합

**IPC 특성:**
- 지연 시간 극히 낮음 (1-2 μs)
- 같은 머신의 두 프로세스 간 통신
- 거의 동시에 read/write 이벤트 발생

**Strand의 문제점:**
- 모든 핸들러를 순차 실행 강제
- `on_read_complete()` 처리 중에는 `on_timer()`, `on_write_complete()` 대기
- IPC 초고속 환경에서 불필요한 직렬화 → **처리량 저하**
- 대기 중인 핸들러 누적 → **Deadlock 위험 증가**

### Hypothesis B: post() 변경이 타이밍 악화

**dispatch vs post:**
```cpp
// Phase 3: dispatch (이미 strand 안이면 즉시 실행)
boost::asio::dispatch(*_strand, [this]() { restart_input_internal(); });

// Phase 4: post (항상 큐에 넣음)
boost::asio::post(*_strand, [this]() { restart_input_internal(); });
```

**post의 부작용:**
- `restart_input()`이 호출되어도 **즉시 실행 안 됨**
- 큐에 들어가서 대기 → 다른 핸들러들이 먼저 실행될 수 있음
- IPC 초고속 환경에서 **타이밍 역전** 발생 가능
- `session`이 `restart_input()` 호출했는데 실제 재시작은 한참 뒤 → **Deadlock**

### Hypothesis C: Strand Overhead가 임계점 초과

**Strand의 오버헤드:**
- 모든 핸들러 호출 시 atomic operation (executor 확인)
- 핸들러 큐잉/디큐잉 오버헤드
- 순차 실행으로 인한 병렬성 손실

**IPC 환경에서:**
- 메시지 처리 시간: ~400ns
- Strand 오버헤드: ~100-200ns (추정)
- **25-50% 오버헤드** → 처리량 저하 → 버퍼 누적 → Deadlock

### Hypothesis D: 잘못된 직렬화 범위

**현재 구현:**
- `asio_engine` 내부의 모든 핸들러를 직렬화
- 하지만 `session`이 `restart_input()` 호출하는 시점은 외부

**문제점:**
```
session (외부 스레드) → restart_input() 호출
  ↓ post to strand
strand queue: [on_read_complete, on_write_complete, on_timer, restart_input_internal]
  ↓ 순차 실행
restart_input_internal이 마지막에 실행됨
```

**session이 원하는 시점에 즉시 재시작 안 됨!**
- session: "지금 restart_input 해줘" (backpressure 해제됨)
- engine: "잠깐, 큐에 있는 다른 것들 먼저 처리하고..." (수백 μs 지연)
- 그 사이 새로운 `async_read` 완료 → 또 pending_buffers 쌓임 → **Deadlock**

## 결론

### Strand 접근은 실패

**명백한 증거:**
- Strand 없음 (Phase 2a): **70%**
- Partial Strand (Phase 3): **60%**
- Complete Strand (Phase 4): **30%**

**일관된 패턴:** Strand 강화 = 성능 악화

### Codex/Gemini 분석의 오류

**그들의 주장:**
1. "Partial Strand가 문제" ❌ → Complete Strand가 더 나쁨
2. "post()로 일관성 확보" ❌ → post()가 타이밍 악화
3. "완전 직렬화로 100% 달성 가능" ❌ → 30%로 재앙

**근본 오류:**
- IPC 초고속 환경의 특성을 간과
- 직렬화 오버헤드를 과소평가
- `restart_input()` 호출 시점의 중요성 무시

## 긴급 질문사항

### Codex에게:
1. **왜 Complete Strand가 Partial Strand보다 30%p 더 나쁜가?**
   - 이론적으로는 완전 직렬화가 안전해야 하는데
   - 실제로는 절반으로 성공률이 떨어짐
2. **post() 변경이 문제인가?**
   - dispatch로 되돌려야 하는가?
   - 아니면 post 자체가 IPC에 부적합한가?
3. **Strand 오버헤드가 IPC 처리량을 임계점 아래로 떨어뜨린 것인가?**
   - 400ns 메시지 처리 + 200ns Strand 오버헤드 = 150% 지연?
4. **session의 restart_input() 호출이 post로 큐잉되는 것이 핵심 문제인가?**
   - 즉시 실행 vs 큐잉의 타이밍 차이가 Deadlock을 유발?

### Gemini에게:
1. **Phase 4 구현이 제안한 내용과 정확히 일치하는가?**
   - 제안한 코드와 실제 구현을 비교 검증
2. **왜 예상과 정반대의 결과가 나왔는가?**
   - 분석의 어떤 가정이 잘못되었는가?
3. **Strand 접근 자체가 IPC에 부적합한가?**
   - TCP에서는 효과가 있지만 IPC에서는 해로운가?
4. **이 결과를 어떻게 해석해야 하는가?**
   - 완전 직렬화가 더 나쁘다는 것은 무엇을 의미하는가?

## 제안하는 다음 단계

### Option A: Phase 2a로 완전 롤백 (Strand 포기)

**근거:**
- Phase 2a (70%) > Phase 3 (60%) > Phase 4 (30%)
- Strand는 해결책이 아님이 명백함

**작업:**
1. `src/asio/asio_engine.hpp`: `_strand` 멤버 제거
2. `src/asio/asio_engine.cpp`: 모든 `bind_executor` 제거
3. `restart_input()`: 원래대로 복원 (dispatch/post 없이 직접 로직 실행)
4. 2K/10K 재테스트 → 70% 복원 확인

### Option B: Phase 3 부분 롤백 (dispatch만 유지)

**가설:** post()가 주범일 수 있음

**작업:**
1. `restart_input()`: post → dispatch로 변경
2. Timer/Handshake 래핑은 유지
3. 2K 테스트 → 60%로 복원되는지 확인

### Option C: Phase 2a + Option B (Speculative Read) 시도

**근거:**
- Strand는 실패
- 원래 계획대로 Speculative Read 접근 시도

**작업:**
1. Phase 2a로 롤백 (70% 복원)
2. Speculative Read 구현 시작
   - `i_asio_transport`에 `read_some()` 추가
   - 5개 transport 모두 구현
   - Backpressure 해제 시 즉시 `read_some()` 호출

**예상 시간:** 1-2주

### Option D: 상세 디버그 + 근본 원인 재분석

**작업:**
1. Phase 2a, 3, 4 각각에서 strace/perf 프로파일링
2. Strand 오버헤드 정확한 측정
3. 타이밍 분석: dispatch vs post 차이
4. `restart_input()` 호출 시점과 실행 시점 로그

## 요청사항

**현재 Phase 4 Complete Strand 결과를 분석하고 다음 중 하나를 권장해주세요:**

**Option A**: Strand 포기, Phase 2a 롤백 (70% 복원)
**Option B**: dispatch만 되돌리기 (Phase 3.5 시도)
**Option C**: Strand 포기, Speculative Read 구현 시작
**Option D**: 상세 프로파일링으로 근본 원인 재분석

분석 결과를 각각:
- `docs/team/20260116_ipc-deadlock-debug/codex_phase4_analysis.md`
- `docs/team/20260116_ipc-deadlock-debug/gemini_phase4_analysis.md`

에 작성해주세요.

**특히 중요한 질문: 왜 완전한 직렬화가 부분 직렬화보다 2배 더 나쁜가?**
