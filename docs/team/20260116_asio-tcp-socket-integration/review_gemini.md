# Review: ASIO tcp::socket 통합 리팩토링 계획

## 전반적인 평가
계획은 Windows의 `WSAPoll` 제거와 `tcp::socket` 기반의 통일된 인터페이스 구축이라는 목표에 잘 부합합니다. 특히 `tcp_transport`가 소켓 소유권을 가져가고, `asio_poller`가 관찰자(Observer) 역할만 수행하는 구조는 수명 관리 관점에서 긍정적입니다.

그러나 **비-TCP 소켓(Signaler/Mailbox) 지원**과 **기존 폴러 인터페이스와의 호환성** 측면에서 중요한 리스크가 발견되었습니다.

## 상세 검토 결과

### 1. `poll_entry_t` 구조 및 `signaler` 호환성 (Critical)
- **계획 내용**: `poll_entry_t`의 descriptor 필드를 `tcp::socket` 기반으로 변경하고, Unix `stream_descriptor`를 제거한다고 명시되어 있습니다.
- **문제점**: ZMQ의 `signaler_t`(스레드 간 신호 전달, `mailbox` 등에서 사용)는 플랫폼에 따라 `eventfd` (Linux), `pipe` (Unix), 또는 `socketpair`를 사용합니다.
  - Linux의 `eventfd`나 Unix의 `pipe`는 `tcp::socket`으로 래핑할 수 없습니다.
  - 계획대로 `stream_descriptor`를 완전히 제거하고 `poll_entry_t`가 `tcp::socket`만 처리하도록 변경하면, `signaler` 등록 시점에 컴파일 에러가 발생하거나 런타임 오동작이 발생합니다.
- **개선 제안**:
  - `asio_poller`는 **TCP 소켓**(`tcp::socket`)과 **일반 FD**(`stream_descriptor` 등)를 모두 지원하는 구조(Variant 또는 다형성)여야 합니다.
  - 또는, `signaler` 등록용 `add_fd` 경로는 기존 `stream_descriptor` 로직을 유지하고, `tcp_transport`용 경로는 `tcp::socket`을 사용하는 하이브리드 접근이 필요합니다.

### 2. `add_fd` 인터페이스와 Duck Typing (Major)
- **계획 내용**: `asio_poller` 등록 시 `tcp::socket` 전달로 통일.
- **문제점**: ZMQ의 `poller_t` 컨셉(Duck Typing)은 `handle_t add_fd(fd_t fd, ...)` 시그니처를 요구합니다. ZMQ 코어 엔진은 `fd_t`(int/SOCKET)를 사용하여 폴러에 등록을 시도합니다.
  - `asio_poller`의 `add_fd` 시그니처를 `tcp::socket*`으로 변경하면, 표준 인터페이스를 따르는 다른 컴포넌트와의 호환성이 깨질 수 있습니다.
- **개선 제안**:
  - `add_fd(fd_t)` 인터페이스는 유지하되, 내부적으로 해당 `fd`가 `tcp_transport`가 관리하는 소켓인지 식별할 수 있는 메커니즘이 필요합니다. (또는 `tcp_transport` 전용의 별도 등록 함수 `add_socket`을 추가하고, `tcp_transport`만 이를 사용하도록 변경)

### 3. 소켓 수명 관리 (Ownership)
- **계획 내용**: `tcp_transport`가 소유하고 `asio_poller`는 참조만 유지.
- **확인 필요**: `tcp_transport`가 파괴될 때 `asio_poller`에 등록된 항목(`poll_entry_t`)이 즉시 제거되거나 무효화되는지 보장해야 합니다.
  - `tcp_transport` 소멸자에서 `poller->rm_fd()`가 호출되겠지만, 이미 `async_wait`가 걸려 있는 상태라면 콜백이 `operation_aborted`로 실행됩니다. 이때 콜백 내부에서 `poll_entry_t` 포인터에 접근하면 UAF(Use-After-Free)가 발생할 수 있습니다.
  - `asio_poller`가 `poll_entry_t` 메모리를 관리하더라도, 콜백 람다가 `poll_entry_t`를 캡처하는 방식에 주의가 필요합니다.

### 4. Windows `async_wait` vs `WSAPoll`
- **평가**: `WSAPoll`을 제거하고 `async_wait` (Reactor 패턴)를 사용하는 것은 올바른 방향입니다. Windows IOCP 환경에서 `async_read/write` (Proactor)가 더 효율적일 수 있으나, 기존 ZMQ 구조(Reactor)를 유지하기 위해서는 `async_wait`가 적절한 선택입니다.

## 수정 권고안 (Action Items)
1.  **`signaler` 지원 방안 추가**: `poll_entry_t`가 `tcp::socket` 뿐만 아니라 `descriptor`(Unix) 또는 `handle`(Windows non-socket)을 처리할 수 있도록 공용체/Variant 구조를 고려하거나, `stream_descriptor` 제거 범위를 "TCP 전송 계층"으로 한정하십시오.
2.  **`add_fd` 호환성 유지**: `asio_poller`에 `add_fd(fd_t)`와 `add_socket(tcp::socket&)` 오버로딩을 제공하는 방안을 검토하십시오.
3.  **테스트 범위 확대**: `mailbox` 및 `signaler` 동작 테스트(스레드 간 깨우기)를 필수 검증 항목에 포함하십시오.
