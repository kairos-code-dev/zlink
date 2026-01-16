# IPC 데드락 해결 구현 계획

## 팀 협업 결과 종합

### Codex 분석
- **문제**: Read 재무장 누락 (missed wakeup)
- **원인**: `_read_pending` 체크만으로는 IPC 초고속 환경에서 불충분
- **제안**: libzmq의 Speculative Read 패턴 적용

### Gemini 검증 및 발견
- **Codex 분석 승인**: 정확한 분석
- **중요 발견**: `i_asio_transport`에 `read_some()` 메서드가 없어서 **Speculative Read는 즉시 구현 불가능**
- **구체적 시나리오 발견**: Pending Buffer 고아 현상 (Step 2-4 race condition)
- **즉시 적용 가능한 대안 제시**:
  1. Double-check 로직
  2. Strand 직렬화

## 채택된 해결 방안

### Phase 1: Double-Check 로직 (즉시 적용) ✅

**선택 이유**:
- 가장 간단 (5-10분 구현)
- 인터페이스 변경 불필요
- 즉시 효과 확인 가능
- Gemini가 검증한 race condition 직접 해결

**구현**:
```cpp
// src/asio/asio_engine.cpp - restart_input() 끝부분
bool zmq::asio_engine_t::restart_input ()
{
    // ... 기존 pending buffers 처리 로직 ...

    //  All pending data processed successfully
    _input_stopped = false;
    _session->flush ();

    ENGINE_DBG ("restart_input: completed, input resumed");

    //  CRITICAL FIX: Double-check pending buffers
    //  Race condition: on_read_complete() may have added data while we were processing
    //  If so, we need to drain it immediately to prevent orphaned buffers
    if (!_pending_buffers.empty()) {
        ENGINE_DBG ("restart_input: race detected, %zu buffers added during processing",
                    _pending_buffers.size());
        //  Recursively process newly added buffers
        //  This ensures no data is left orphaned
        return restart_input();
    }

    //  CRITICAL FIX for IPC deadlock: Ensure async read is active.
    if (!_read_pending) {
        //  No async_read pending - start one
        start_async_read ();
    }

    return true;
}
```

**예상 효과**:
- 40% 실패율 → 5-10% 수준으로 개선
- Pending buffer 고아 현상 방지
- 완전한 해결은 아니지만 대부분의 경우 수정

### Phase 2: Strand 직렬화 (필요시)

**조건**: Phase 1 후에도 데드락 발생 시

**구현**:
```cpp
// src/asio/asio_engine.hpp
class asio_engine_t {
    boost::asio::io_context::strand _strand;
};

// src/asio/asio_engine.cpp
bool zmq::asio_engine_t::restart_input ()
{
    //  Ensure execution on IO thread
    if (!_io_context->get_executor().running_in_this_thread()) {
        boost::asio::post(*_io_context, [this]() {
            restart_input();
        });
        return true;
    }

    //  ... 기존 로직 (Phase 1 포함) ...
}
```

**예상 효과**: 완전한 race condition 제거

### Phase 3: Speculative Read (장기 계획)

**전제 조건**: `i_asio_transport` 인터페이스 확장 필요

**단계**:
1. `i_asio_transport`에 `read_some()` 메서드 추가
2. 모든 transport (tcp, ipc, ssl, ws, wss) 구현
3. `restart_input()`에 동기 read 추가

**예상 작업량**: 1-2주 (인터페이스 설계 + 5개 transport 구현 + 테스트)

## 구현 순서

### Step 1: Phase 1 구현 (10분)

```bash
# 1. 코드 수정
vim src/asio/asio_engine.cpp  # Double-check 로직 추가

# 2. 빌드
./build-scripts/linux/build.sh x64 ON

# 3. 테스트
for i in {1..10}; do
    BENCH_MSG_COUNT=2000 timeout 10 ./build/bin/comp_zlink_pair zlink ipc 64 || echo "FAIL $i";
done
```

**성공 기준**: 10회 중 8회 이상 성공 (80%+)

### Step 2: 전체 패턴 검증 (5분)

```bash
for pattern in pair pubsub dealer_dealer router_router; do
    echo "=== $pattern ==="
    BENCH_MSG_COUNT=10000 ./build/bin/comp_zlink_${pattern} zlink ipc 64
done
```

**성공 기준**: 모든 패턴 1회 성공

### Step 3: 200K 메시지 테스트 (필요시)

```bash
BENCH_MSG_COUNT=200000 timeout 60 ./build/bin/comp_zlink_pair zlink ipc 64
```

**성공 기준**: 타임아웃 없이 완료

### Step 4: Phase 2 구현 (조건부)

**조건**: Step 2에서 실패율 20% 이상 시

**작업**: Strand 직렬화 추가 (30분)

## 검증 방법

### 1. 기본 안정성 테스트
```bash
# 2K 메시지 10회 반복
for i in {1..10}; do
    BENCH_MSG_COUNT=2000 timeout 10 ./build/bin/comp_zlink_pair zlink ipc 64
done | grep -c "RESULT"
# 기대값: 8개 이상
```

### 2. 대용량 테스트
```bash
# 200K 메시지 5회 반복
for i in {1..5}; do
    BENCH_MSG_COUNT=200000 timeout 60 ./build/bin/comp_zlink_pair zlink ipc 64 || echo "FAIL $i";
done
# 기대값: 실패 0-1회
```

### 3. 성능 회귀 검증
```bash
# TCP도 정상 작동 확인
./build/bin/comp_zlink_pair zlink tcp 64
./build/bin/comp_zlink_router_router zlink tcp 64
```

## 의사결정 트리

```
Phase 1 구현 → 테스트
  ├─ 80%+ 성공 → 전체 패턴 테스트
  │    ├─ 모두 성공 → 완료 ✅
  │    └─ 일부 실패 → Phase 2 (Strand)
  └─ 80% 미만 → Phase 2 (Strand) 즉시 진행

Phase 2 구현 → 테스트
  ├─ 100% 성공 → 완료 ✅
  └─ 실패 지속 → Phase 3 (Speculative Read) 장기 계획
```

## 예상 일정

| Phase | 작업 | 시간 | 누적 |
|-------|------|------|------|
| Phase 1 | Double-check 구현 | 10분 | 10분 |
| | 테스트 | 5분 | 15분 |
| Phase 2 (조건부) | Strand 구현 | 30분 | 45분 |
| | 테스트 | 10분 | 55분 |
| Phase 3 (선택) | 인터페이스 설계 | 2-3일 | - |
| | Transport 구현 | 5-7일 | - |
| | Speculative Read | 1일 | - |

**최소 목표**: Phase 1 완료 (15분)
**현실적 목표**: Phase 2 완료 (1시간)
**이상적 목표**: Phase 3 완료 (1-2주, 장기)

## 다음 단계

**즉시 실행**: Phase 1 구현
- 파일: `src/asio/asio_engine.cpp`
- 수정 위치: `restart_input()` 함수 끝부분 (line ~1065)
- 예상 시간: 10분
- 테스트: 2K, 10K 메시지

구현을 시작할까요?
