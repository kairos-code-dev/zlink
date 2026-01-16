# Gemini 리뷰 결과 (2026-01-16)

## 리뷰 요약

이 계획은 syscall 오버헤드를 줄여 성능을 개선하려는 올바른 방향을 잡고 있으나, **현재 코드의 아키텍처(Proactor)와 계획의 가정(Reactor) 사이에 불일치**가 있어 수정이 필요합니다.

---

## 1. 아키텍처 불일치 수정 필요 (Proactor vs Reactor)

**현황**:
- `src/asio/asio_engine.cpp`는 `async_read`/`async_write`를 사용하는 **Proactor 패턴**입니다.

**문제**:
- 계획서의 **전략 B**는 `in_event()`/`out_event()`를 언급하며 Reactor 패턴(poll 후 read/write)을 가정하고 있습니다.
- Proactor 모델에서는 `on_read_complete`/`on_write_complete` 핸들러 내에서 배칭을 처리해야 합니다.

**제안**:
- `in_event`/`out_event` 수정을 `on_read_complete` 내 루프 처리 및 `async_write` 전 `gather` 버퍼 구성으로 계획을 변경해야 합니다.

### 수정 전 (전략 B):
> - in_event()/out_event() 내부 배칭 강도를 재검토.

### 수정 후 (전략 B):
> - **Proactor 핸들러 배칭**: `asio_engine::on_read_complete`에서 수신 버퍼에 있는 **여러 메시지를 루프를 돌며 한 번에 처리**하여 엔진-소켓 간 왕복 횟수 감소.
> - **Write Batching**: `process_output`에서 즉시 `async_write`를 호출하지 않고, 출력 큐에 쌓인 메시지들을 `iovec` (buffer sequence)으로 묶어 **단일 `async_write` 호출**.

---

## 2. tcp::socket은 이미 사용 중 (전략 C 재검토)

**현황**:
- `src/asio/tcp_transport.hpp`를 보면 이미 `boost::asio::ip::tcp::socket`을 사용하고 있습니다.
- `stream_descriptor`는 `asio_poller`(이벤트 대기용)나 `ipc_transport` (POSIX)에서 주로 사용됩니다.

**문제**:
- TCP 성능이 문제라면 `stream_descriptor`로의 변환 오버헤드보다는, `async_write` 호출 빈도 자체가 문제일 가능성이 높습니다.

**제안**:
- 전략 C를 "IPC Transport의 `stream_descriptor` 최적화"로 변경하거나,
- TCP의 경우 **Scatter-Gather I/O (writev)** 활용 여부를 점검하는 것으로 구체화해야 합니다.

---

## 3. Scatter-Gather (Vectorized I/O) 누락

**내용**:
- Syscall을 줄이는 가장 확실한 방법은 여러 작은 메시지(헤더+바디, 또는 여러 메시지)를 `std::vector<boost::asio::const_buffer>`로 묶어 한 번의 `async_write`로 보내는 것입니다.

**제안**:
- **전략 D: Scatter-Gather 구현**을 추가하여, 단일 버퍼 복사(`memcpy`) 대신 ASIO의 buffer sequence 기능을 활용하도록 명시해야 합니다.

---

## 중요 확인 사항

`ROUTER` 패턴의 성능 저하는 작은 메시지가 많을 때 ASIO의 `async_read`/`write` 오버헤드(메모리 할당, 스케줄링)가 누적되어 발생합니다.

**핵심**: 단순 syscall 횟수뿐만 아니라 **핸들러 호출 횟수** 자체를 줄이는 배칭이 핵심입니다.

---

## 권장 수정사항

1. 전략 B를 Proactor 패턴에 맞게 재작성 (`on_read_complete`, `async_write` 전 gather)
2. 전략 C를 구체화 (IPC 최적화 또는 TCP Scatter-Gather I/O)
3. 전략 D 추가 (Scatter-Gather I/O 명시적 구현)
