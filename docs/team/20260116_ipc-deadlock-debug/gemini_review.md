# IPC 데드락 분석 리뷰 및 검증 (Gemini)

## 개요
Codex가 작성한 `20260116_ipc-deadlock-debug/codex_analysis.md`에 대한 리뷰 및 코드베이스 검증 결과입니다. 분석 결과는 정확하며, 데드락의 원인이 **`restart_input`과 `on_read_complete` 사이의 경쟁 상태(Race Condition)로 인한 Pending Buffer 고아 현상(Orphaned Buffer)**임이 확인되었습니다.

## 1. 분석 검증 (Correctness)
**Codex의 분석은 정확합니다.** 현재 `src/asio/asio_engine.cpp`의 구현을 검토한 결과, 다음과 같은 치명적인 시나리오가 발생할 수 있습니다.

### 확인된 데드락 시나리오
1.  **Backpressure 상태**: `_input_stopped = true`인 상황.
2.  **`restart_input` 진입 (Thread A)**: `_pending_buffers`를 비움(Drain). 버퍼가 비어있다고 판단.
3.  **`on_read_complete` 실행 (Thread B - IO Thread)**: 이 시점에 네트워크 패킷 도착.
    *   `_input_stopped`가 여전히 `true`이므로 데이터를 `_pending_buffers`에 추가(Push).
    *   `start_async_read()` 호출하여 `_read_pending = true` 설정.
    *   종료.
4.  **`restart_input` 계속 (Thread A)**:
    *   `_input_stopped = false`로 설정 (Backpressure 해제).
    *   **문제 발생**: `_pending_buffers`는 Step 3에서 추가된 데이터가 있지만, Step 2에서 이미 검사했으므로 다시 확인하지 않음.
    *   `_read_pending`을 확인. Step 3에서 `true`로 설정되었으므로 추가 조치 없이 리턴.
5.  **결과 (Deadlock)**:
    *   `_pending_buffers`에 데이터가 남아있음.
    *   `_input_stopped`는 `false`.
    *   `_read_pending`은 `true` (다음 패킷 대기 중).
    *   **남아있는 데이터는 다음 네트워크 패킷이 도착하여 `on_read_complete`가 호출될 때까지 영원히 처리되지 않음.** (Message Loss / Stall)

## 2. 해결책 평가 (Solution Assessment)

### A. ASIO Strand 직렬화 (권장)
*   **평가**: **가장 확실하고 안전한 해결책입니다.**
*   **이유**: `restart_input`과 `on_read_complete`가 상호 배제(Mutual Exclusion)되어야만 위 시나리오의 Step 2와 3이 섞이는 것을 막을 수 있습니다.
*   **구현**: `restart_input` 내부 로직을 `io_context`의 `strand`를 통해 `dispatch` 하거나, 엔진 전체를 단일 Strand로 보호해야 합니다.

### B. Speculative Read (동기 읽기)
*   **평가**: **성능상 매우 유효하나, 선행 작업이 필요합니다.**
*   **제약 사항**: 현재 `src/asio/i_asio_transport.hpp` 인터페이스를 확인한 결과, **`read_some` (동기 읽기) 메서드가 존재하지 않습니다.** (`async_read_some`만 존재)
*   **조치 필요**: 이 방식을 적용하려면 `i_asio_transport` 인터페이스 확장 및 모든 Transport(`tcp`, `ipc`, `ssl`, `ws`, `wss`)에 대한 `read_some` 구현이 선행되어야 합니다.

## 3. 추가 고려 사항 및 제안

### 1) 즉시 적용 가능한 수정 (Hotfix)
인터페이스 확장이 필요한 Speculative Read보다, **Strand를 이용한 직렬화**를 먼저 적용하여 데드락을 해소해야 합니다.

```cpp
// 가상 코드 예시 (src/asio/asio_engine.cpp)
bool zmq::asio_engine_t::restart_input() {
    // 이미 Strand 내부라면 즉시 실행, 아니면 Strand로 dispatch
    // (현재 zlink는 io_context가 사실상 strand 역할을 하지만,
    // 외부 스레드에서 restart_input이 호출될 경우를 대비해 explicit strand나 dispatch 필요)
    if (!_io_context->get_executor().running_in_this_thread()) {
         boost::asio::post(*_io_context, [this](){ restart_input(); });
         return true;
    }
    // ... 기존 로직 ...
}
```
*주의: `zlink`의 `io_thread` 모델을 고려할 때, `dispatch`를 사용하여 IO 스레드 내에서 즉시 실행을 보장해야 합니다.*

### 2) `restart_input` 로직 보강
Strand 도입 전이라도, `restart_input`의 끝부분에서 `_pending_buffers`를 **한 번 더 체크**하는 로직(Double-check)이 추가되면 위 레이스 컨디션의 확률을 크게 낮출 수 있습니다.

```cpp
// src/asio/asio_engine.cpp:1065 부근
// _input_stopped = false 설정 후:
if (!_pending_buffers.empty()) {
    // 레이스로 인해 처리 도중 데이터가 쌓였다면 다시 루프
    return restart_input();
}
```

## 4. 결론
1.  **Codex 분석 승인**: 분석 내용은 정확하며 데드락 원인을 올바르게 지적했습니다.
2.  **구현 방향**:
    *   **Priority 1 (안정성)**: `restart_input`을 `io_context` 스레드로 강제 직렬화 (`dispatch` 활용).
    *   **Priority 2 (성능)**: `i_asio_transport`에 `read_some` 추가 후 Speculative Read 구현.

**다음 단계로 `docs/team/20260116_ipc-deadlock-debug/plan.md` (구현 계획) 작성을 권장합니다.**
