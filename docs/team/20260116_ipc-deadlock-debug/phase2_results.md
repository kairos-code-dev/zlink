# Phase 2 구현 결과 및 현재 상황

## 구현된 수정사항

### Phase 1: Double-check (초기)
- `_input_stopped = false` **후에** `_pending_buffers` 재확인
- 문제: 재귀 호출 시 `zmq_assert(_input_stopped)` 실패
- **결과**: 50% 성공률

### Phase 2a: Double-check 순서 변경
- `_input_stopped = false` **전에** `_pending_buffers` 체크
- 버퍼 있으면 재귀 호출 (이때 _input_stopped는 아직 true)
- **결과**: 70% 성공률

### Phase 2b: flush() 후 Double-check
- `_input_stopped = false; _session->flush()` 후 재확인
- 버퍼 있으면 `_input_stopped = true`로 재설정 후 재귀
- **결과**: 70% 성공률 (변화 없음)

### Phase 2c: 무조건 start_async_read() 호출
- `if (!_read_pending)` 조건 제거
- 항상 `start_async_read()` 호출
- **결과**: 50% 성공률 (악화!)

## 테스트 결과 요약

| 수정 단계 | 2K 메시지 성공률 | 10K 메시지 성공률 |
|----------|----------------|-----------------|
| **Phase 1** (Double-check 후) | 50% | 미테스트 |
| **Phase 2a** (Double-check 전) | 70% | 0% |
| **Phase 2b** (flush 후 check) | 70% | 미테스트 |
| **Phase 2c** (무조건 async_read) | 50% | 미테스트 |

## 현재 코드 상태

`src/asio/asio_engine.cpp` - `restart_input()` 끝부분:

```cpp
// Phase 2b: flush() 후 Double-check
_input_stopped = false;
_session->flush ();

if (!_pending_buffers.empty()) {
    ENGINE_DBG ("restart_input: race detected AFTER flush, %zu buffers accumulated, re-entering stopped mode",
                _pending_buffers.size());
    _input_stopped = true;
    return restart_input();
}

// Phase 2c: 무조건 start_async_read() 호출
start_async_read ();  // 조건 제거함

return true;
```

## 발견된 문제점

1. **Double-check가 일부만 도움됨**: 70% 성공률은 여전히 30% 실패
2. **10K 메시지는 여전히 0%**: 대용량 메시지에서는 완전 실패
3. **무조건 async_read는 오히려 악화**: `_read_pending` 체크 제거가 문제 악화

## 근본 원인 추정

Codex와 Gemini의 초기 분석:
1. **Read 재무장 누락** (missed wakeup)
2. **Pending buffer 고아 현상**
3. **on_read_complete()와 restart_input() 사이의 race**

하지만 Double-check만으로는 불충분합니다.

### 가능한 추가 원인:

#### Hypothesis 1: ASIO 단일 thread 가정의 오류
- 현재 코드는 `on_read_complete()`와 `restart_input()`이 같은 thread에서 실행된다고 가정
- 하지만 실제로는 다른 executor에서 실행될 수 있음?
- Gemini가 제안한 **Strand 직렬화**가 필요한 이유

#### Hypothesis 2: flush()의 reentrancy
- `_session->flush()`가 내부적으로 다른 command 처리
- 이 과정에서 `restart_input()`이 다시 호출될 수 있음?
- 재귀 depth 제한이 필요할 수 있음

#### Hypothesis 3: start_async_read()의 타이밍 문제
- `start_async_read()` 호출 시점에 이미 데이터가 소켓 버퍼에 있음
- ASIO는 즉시 callback을 트리거하지 않고 다음 event loop에서 처리
- IPC는 너무 빨라서 event loop 돌아오기 전에 더 많은 데이터 도착
- 결과: 소켓 버퍼 오버플로우 또는 데이터 손실?

#### Hypothesis 4: Speculative Read 필수
- libzmq는 `in_event_internal()`로 **동기** read 시도
- ASIO는 **비동기** read만 가능 (`i_asio_transport`에 `read_some()` 없음)
- 비동기 read는 다음 event loop까지 지연 → IPC 초고속 환경에서 치명적
- **해결책**: `i_asio_transport` 인터페이스 확장 필요 (Gemini 지적)

## 다음 단계 질문

### Codex에게:
1. Hypothesis 1-4 중 어느 것이 가장 유력한가?
2. Strand 직렬화를 구현하면 해결되는가?
3. 아니면 근본적으로 Speculative Read (동기 read)가 필수인가?
4. `i_asio_transport`에 `read_some()` 추가하는 것이 현실적인가?

### Gemini에게:
1. Phase 2a-c 결과를 보면, 어떤 접근이 가장 올바른가?
2. 70% 성공률에서 멈춘 이유는 무엇인가?
3. Strand 직렬화의 구체적인 구현 방법은?
4. 대안으로 재귀 depth 제한이나 다른 방법이 있는가?

## 요청사항

현재 상황을 분석하고 다음 중 하나를 권장해주세요:

**Option A: Strand 직렬화**
- `restart_input()`을 io_context에 `post()`
- `on_read_complete()` 핸들러도 같은 strand에서 실행
- 완전한 직렬화 보장

**Option B: Speculative Read 구현**
- `i_asio_transport`에 `read_some()` 메서드 추가
- 5개 transport (tcp, ipc, ssl, ws, wss) 모두 구현
- `restart_input()`에서 동기 read 시도

**Option C: 다른 접근**
- 재귀 depth 제한
- event loop 강제 yield
- 기타 아이디어

**Option D: 현 상태 수용**
- 70% 성공률로 수용
- Trade-off 인정

분석 결과를 각각:
- `docs/team/20260116_ipc-deadlock-debug/codex_phase2_analysis.md`
- `docs/team/20260116_ipc-deadlock-debug/gemini_phase2_analysis.md`

에 작성해주세요.
