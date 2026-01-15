# ASIO tcp::socket 통합 계획 검증 (Codex)

## 결론 요약
- Gemini의 지적(신호용 FD는 tcp::socket으로 래핑 불가)은 **맞음**. POSIX에서 signaler는 `eventfd`/`pipe`/`socketpair(AF_UNIX)` 기반이며 TCP 소켓이 아니다.
- 계획대로 `stream_descriptor`를 완전히 제거하고 `poll_entry_t`를 `tcp::socket` 전용으로 바꾸면, **signaler 등록이 깨진다** (컴파일 실패 또는 런타임 등록 실패).
- 적절한 해결책은 **하이브리드/Variant** 접근: `add_fd(fd_t)` 경로는 유지하고, TCP 전용은 별도 `add_socket(tcp::socket&)` 또는 `variant` 기반으로 통합한다.

## 1) signaler(eventfd/pipe/socketpair)가 tcp::socket으로 래핑 불가한가?
**예.** 현재 구현상 signaler는 플랫폼에 따라 다음 FD를 생성한다.
- Linux: `eventfd` (비소켓 FD) (`src/ip.cpp`의 `make_fdpair`)
- POSIX 일반: `socketpair(AF_UNIX)` (`src/ip.cpp`)
- Windows: TCP loopback 기반 socketpair 또는 AF_UNIX(IPC 지원 시) (`src/ip.cpp`)
- 사용처: `signaler_t`는 `make_fdpair` 결과를 그대로 `fd_t`로 노출 (`src/signaler.cpp`, `src/signaler.hpp`)

`boost::asio::ip::tcp::socket`은 **AF_INET/AF_INET6의 TCP 소켓 전용**이므로 `eventfd`, `pipe`, `AF_UNIX socketpair`는 **tcp::socket에 assign 불가**다. 따라서 Gemini의 지적은 정확하다.

## 2) stream_descriptor를 완전히 제거하면 signaler 등록이 실패하는가?
**예.** 현재 `asio_poller_t::add_fd(fd_t, ...)`는 POSIX에서 `boost::asio::posix::stream_descriptor`로 FD를 래핑한다 (`src/asio/asio_poller.hpp`, `src/asio/asio_poller.cpp`).

`io_thread_t`는 mailbox의 signaler FD를 `add_fd`로 등록한다 (`src/io_thread.cpp`, `src/mailbox.cpp`). 이 FD는 위 1)에서 확인한 비-TCP FD일 수 있으므로:
- `poll_entry_t`가 `tcp::socket`만 보유하도록 바꾸면,
  - **타입/할당 불가**(컴파일 단계에서 막히거나),
  - **런타임 등록 실패**(native_handle 할당 실패)로 이어진다.

즉, `stream_descriptor` 제거는 **signaler 등록 실패**로 직결된다.

## 3) 해결 방법으로 적절한 접근
다음 중 **하이브리드/Variant 설계**가 현실적인 최소 변경이다.

### 권장 접근: 하이브리드(기존 add_fd 유지 + TCP 전용 경로 추가)
- `add_fd(fd_t)`는 그대로 유지하고 내부에서 `stream_descriptor`로 관리
  - mailbox/signaler/기타 일반 FD 대응 유지
- TCP 연결은 별도 `add_socket(tcp::socket&)` (혹은 `add_tcp_socket`) 경로를 추가
  - `poll_entry_t`에 `variant`로 `tcp::socket*` 또는 `stream_descriptor`를 보관
  - `async_wait` 호출 시 타입에 따라 분기

### Variant 확장 옵션
- Windows IPC가 AF_UNIX를 사용할 수 있으므로 필요 시
  - `boost::asio::local::stream_protocol::socket*`도 `variant`에 포함
- POSIX에서는 `stream_descriptor`가 eventfd/pipe/AF_UNIX를 모두 커버함

### 비권장: stream_descriptor 완전 제거
- poller의 `add_fd(fd_t)` 계약을 깨며, mailbox/signaler가 동작하지 않음
- ZMQ 코어 코드의 duck-typed poller 인터페이스(`add_fd`, `rm_fd`)와 충돌

## 참고 파일
- `src/ip.cpp` (signaler용 FD 생성: eventfd/socketpair/TCP)
- `src/signaler.cpp`, `src/signaler.hpp` (signaler FD 노출)
- `src/asio/asio_poller.hpp`, `src/asio/asio_poller.cpp` (add_fd/stream_descriptor)
- `src/io_thread.cpp`, `src/mailbox.cpp` (mailbox signaler 등록)
