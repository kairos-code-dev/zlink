# Phase 2 결과 분석 및 권고안

## 1. Phase 2 결과 분석

Phase 2 테스트 결과를 통해 다음과 같은 결론을 도출할 수 있습니다.

### 1.1 Phase 2a (Double-check) vs Phase 2c (Unconditional Read)
- **Phase 2a (70% 성공)**: `_pending_buffers`를 확인하고 조건부로 재시작하는 로직이 기본적인 데이터 흐름을 유지하는 데 효과적임이 증명되었습니다.
- **Phase 2c (50% 성공 - 악화)**: 무조건적인 `start_async_read()` 호출은 오히려 해가 되었습니다. 이는 ASIO의 `async_read`가 이미 pending 상태일 때 중복 호출되거나, 버퍼 상태(`_inpos`, `_insize`)가 정리되지 않은 상태에서 읽기를 시도하여 메모리 오염이나 논리적 race를 유발했기 때문으로 보입니다.

**결론**: "무조건 읽기"보다는 **"정확한 상태 체크 후 읽기"**가 올바른 방향입니다.

### 1.2 70% 성공률의 벽 (The 30% Gap)
Phase 2a와 2b가 모두 70%에서 멈춘 이유는 **단일 스레드 내의 핸들러 실행 순서 보장 부재** 때문일 가능성이 매우 높습니다.
- `restart_input()`은 `session_base` (상위 레이어)에서 호출됩니다.
- `on_read_complete()`는 ASIO `io_context` (하위 레이어)에서 호출됩니다.
- 비록 같은 IO 스레드라 하더라도, 이 두 함수 사이의 **재진입(Reentrancy)**이나 **실행 순서(Interleaving)**가 `strand` 없이 암시적으로 관리되고 있어, 고속 IPC 통신(10K 메시지) 시 미세한 타이밍으로 Race Condition이 발생합니다.

---

## 2. 권고안: Option A (Strand 직렬화)

**강력히 Option A를 권장합니다.**
Speculative Read(Option B)는 근본적인 동기화 문제를 해결하지 못하며, 재귀 제한(Option C)은 임시방편입니다. ASIO 기반 시스템에서 핸들러 간의 Race를 막는 표준적이고 가장 확실한 방법은 **Strand**를 사용하는 것입니다.

### 2.1 구현 목표
- `restart_input` (User/Session context)과 `on_read_complete` (IO context)가 **절대 동시에 실행되지 않도록** 보장합니다.
- 모든 비동기 핸들러(`read`, `write`, `timer`)를 하나의 Strand로 묶습니다.

---

## 3. 구체적인 구현 방법

### 3.1 헤더 파일 수정 (`src/asio/asio_engine.hpp`)

`asio_engine_t` 클래스에 `strand` 멤버를 추가합니다. `io_context`가 생성자 시점에 없으므로 `std::unique_ptr`를 사용합니다.

```cpp
// src/asio/asio_engine.hpp

class asio_engine_t : public i_engine
{
    // ... 기존 코드 ...
private:
    // ... 기존 멤버들 ...

    // Strand for serializing handlers
    // <executor_type>은 io_context::executor_type을 사용
    std::unique_ptr<boost::asio::strand<boost::asio::io_context::executor_type>> _strand;
};
```

### 3.2 소스 파일 수정 (`src/asio/asio_engine.cpp`)

#### A. Strand 초기화 (`plug`)
`plug()` 메서드에서 `_io_context`가 설정된 직후 `_strand`를 초기화합니다.

```cpp
void zmq::asio_engine_t::plug (io_thread_t *io_thread_,
                               session_base_t *session_)
{
    // ... 기존 코드 ...
    _io_context = &poller->get_io_context ();

    // [Gemini] Strand 초기화
    _strand.reset (new boost::asio::strand<boost::asio::io_context::executor_type> (
      _io_context->get_executor ()));

    // ... 이후 코드 ...
}
```

#### B. Async Read/Write 핸들러 래핑
`bind_executor`를 사용하여 모든 비동기 작업을 Strand에 묶습니다.

```cpp
void zmq::asio_engine_t::start_async_read ()
{
    // ... (중략) ...

    if (_transport) {
        // [Gemini] bind_executor로 핸들러 래핑
        _transport->async_read_some (
          _read_buffer_ptr, read_size,
          boost::asio::bind_executor (
            *_strand,
            [this] (const boost::system::error_code &ec, std::size_t bytes) {
                on_read_complete (ec, bytes);
            }));
    }
}

void zmq::asio_engine_t::start_async_write ()
{
    // ... (중략) ...

    if (_transport) {
        // [Gemini] bind_executor로 핸들러 래핑
        _transport->async_write_some (
          _write_buffer.data (), _write_buffer.size(),
          boost::asio::bind_executor (
            *_strand,
            [this] (const boost::system::error_code &ec, std::size_t bytes) {
                on_write_complete (ec, bytes);
            }));
    }
}
```
*Timer(`add_timer`)도 동일하게 `bind_executor`를 적용해야 안전합니다.*

#### C. `restart_input` 직렬화
`restart_input`은 `session` 객체(외부)에서 호출되므로, 로직 전체를 Strand 안으로 `dispatch` 해야 합니다.

1. 기존 `restart_input` 로직을 `restart_input_internal`이라는 private 메서드로 분리하거나 람다로 감쌉니다.
2. `restart_input`은 단순히 `dispatch`만 수행합니다.

```cpp
// 수정된 restart_input 구현
bool zmq::asio_engine_t::restart_input ()
{
    // [Gemini] Strand를 통해 실행 보장
    // dispatch는 현재 스레드가 strand 안에 있다면 즉시 실행하고,
    // 아니라면 post처럼 동작하여 직렬화를 보장합니다.
    boost::asio::dispatch (*_strand, [this]() {
        // 기존 restart_input의 로직을 여기에 위치시킵니다.

        zmq_assert (_input_stopped);

        // ... (기존 로직 전체 복사) ...

        // 주의: restart_input은 원래 bool을 반환하지만,
        // dispatch된 비동기 로직에서는 반환값을 상위로 전달할 수 없습니다.
        // 엔진 구조상 내부에서 error()를 호출하여 처리하면 되므로 무방합니다.
    });

    return true; // 항상 성공으로 간주하고 리턴
}
```

## 4. 기대 효과
이 변경사항이 적용되면:
1. `restart_input` 실행 중에 `on_read_complete`가 끼어들거나, 그 반대의 상황이 **원천적으로 차단**됩니다.
2. 30%의 실패 원인인 미세한 Race Condition이 제거되어 **100% 성공률**을 달성할 것으로 예상됩니다.

## 5. 다른 옵션들에 대한 평가

### Option B (Speculative Read)
- `i_asio_transport` 인터페이스에 `read_some()` 메서드 추가가 필요
- 5개 transport (tcp, ipc, ssl, ws, wss) 모두 구현 필요
- 동기화 문제를 근본적으로 해결하지 못함
- **권장하지 않음** - Strand 적용 후에도 문제가 지속될 경우에만 고려

### Option C (재귀 depth 제한 등)
- 증상 완화에 불과하며 근본 원인 해결이 아님
- 70% 이상 개선 기대 어려움
- **권장하지 않음**

### Option D (현 상태 수용)
- 70% 성공률은 실용적으로 수용 불가능
- 10K 메시지에서 0% 성공률은 심각한 문제
- **권장하지 않음**

## 6. 실행 계획

1. **Phase 3a: Strand 직렬화 구현** (우선순위 1)
   - `asio_engine.hpp`에 `_strand` 멤버 추가
   - `plug()`에서 strand 초기화
   - 모든 async 핸들러를 `bind_executor`로 래핑
   - `restart_input()`을 `dispatch`로 직렬화
   - 예상 작업 시간: 2-3시간

2. **Phase 3b: 테스트 및 검증**
   - 2K 메시지 10회 반복 테스트 → 목표: 100% 성공
   - 10K 메시지 5회 반복 테스트 → 목표: 100% 성공
   - 200K 메시지 테스트 → 목표: 타임아웃 없이 완료

3. **Phase 3c: 실패 시 대안 검토** (조건부)
   - Strand 적용 후에도 문제가 지속되면 Option B 재검토
   - 그러나 Strand로 인한 동기화 문제 해결 가능성이 매우 높으므로 이 단계까지 갈 가능성은 낮음
