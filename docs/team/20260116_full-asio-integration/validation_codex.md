# validation_codex.md

이 문서는 `docs/team/20260116_full-asio-integration/review_gemini.md`의 Critical 3건에 대해 현재 코드베이스를 근거로 검증한 결과를 정리합니다.

## 1. IPC 지원 문제 (stream_descriptor 제거 영향, local::stream_protocol::socket 사용 가능성)

### 현 구현 확인
- IPC 연결/수락은 `boost::asio::local::stream_protocol::socket` 기반으로 동작하며, 연결 성립 후에는 native fd를 엔진에 넘깁니다. 이는 `asio_ipc_connecter_t::create_engine` 및 `asio_ipc_listener_t::create_engine`에서 확인됩니다. (`src/asio/asio_ipc_connecter.cpp:279`, `src/asio/asio_ipc_listener.cpp:273`)
- IPC 데이터 송수신은 POSIX에서 `boost::asio::posix::stream_descriptor`를 사용합니다. `ipc_transport_t`의 POSIX 경로는 `stream_descriptor`에 fd를 직접 바인딩하여 async I/O를 수행합니다. (`src/asio/ipc_transport.hpp:44`, `src/asio/ipc_transport.cpp:96`~)
- ASIO poller 역시 POSIX에서 `boost::asio::posix::stream_descriptor`를 사용해 async_wait를 수행합니다. (`src/asio/asio_poller.hpp:63`, `src/asio/asio_poller.cpp:33`~)

### 검증 결론
- **stream_descriptor를 제거하면 현재 POSIX IPC 경로는 즉시 깨집니다.** IPC 엔진의 I/O가 `ipc_transport_t`의 `stream_descriptor`에 의존하고 있으며, 대체 경로가 없습니다.
- 또한 `asio_poller_t`도 POSIX의 모든 FD를 `stream_descriptor`로 감싸기 때문에, 제거 시 IPC뿐 아니라 TCP 등 전체 폴링 메커니즘에 영향이 있습니다.

### local::stream_protocol::socket 사용 가능성
- POSIX에서도 `boost::asio::local::stream_protocol::socket`을 사용해 AF_UNIX fd를 `assign`하거나 `native_handle`을 넘기는 방식으로 대체 구현이 가능할 여지는 있습니다.
- 다만 현재 `ipc_transport_t`는 POSIX에서 `stream_descriptor`만을 사용하고 있으며, 이를 `local::stream_protocol::socket`으로 교체하려면 다음이 필요합니다.
  - `ipc_transport_t`의 POSIX 경로 변경 (open/close/async_read/async_write/write_some 구현 변경)
  - `asio_poller_t`의 POSIX 경로에서도 `stream_descriptor` 제거 시 대체 가능한 wait 경로 확보
- 결론적으로 **현 상태에서 stream_descriptor 제거는 IPC를 포함한 POSIX I/O 경로에 치명적이며, 대체 구현이 동반되지 않으면 IPC는 깨집니다.**

## 2. Mailbox Race Condition (Double-check 패턴, Lost Wakeup 가능성)

### 현 구현 확인
- 현재 `mailbox_t`는 `signaler_t`와 `ypipe`를 결합한 구조로, `send()`에서 flush 실패 시 signaler를 통해 깨우고 `recv()`에서 signaler를 wait/recv하는 구조입니다. (`src/mailbox.cpp:27`~)
- `scheduled` 플래그 또는 `io_context::post` 기반 큐 스케줄링 로직은 현재 코드에 존재하지 않습니다.

### Lost Wakeup 가능성 검증
- **현재 구현은 signaler 기반이므로 Gemini가 지적한 “scheduled flag 기반 Lost Wakeup” 문제가 직접적으로 발생하지 않습니다.**
- 하지만 계획대로 `io_context::post` + `scheduled` 플래그로 바뀐다면, Gemini가 제안한 double-check 패턴이 없을 경우 **Lost Wakeup이 실제로 가능**합니다.
  - 예: `process_mailbox`가 큐를 비운 뒤 `scheduled=false`로 만들고 종료하려는 순간 다른 스레드가 enqueue 하되 `scheduled` 상태를 이미 true로 보고 post를 건너뛰면, 큐는 남아있지만 실행이 재스케줄되지 않을 수 있습니다.

### Double-check 패턴 타당성
- Gemini가 제안한 **Double-check 패턴은 Lost Wakeup을 피하기 위해 필요한 구조로 타당합니다.**
- 단, 실제 큐 구현(락/원자성/메모리 장벽)에 맞춘 memory order, empty 체크 시점 동기화가 함께 명시되어야 합니다.

## 3. 단계 1/2 의존성 (mailbox.get_fd 호출 위치, 통합 필요성)

### mailbox.get_fd() 호출 위치
- `io_thread_t`에서 폴러 등록용으로 사용: `src/io_thread.cpp:19`
- `reaper_t`에서 폴러 등록용으로 사용: `src/reaper.cpp:22`
- `socket_base_t`에서 소켓 생성 시 fd 유효성 검사: `src/socket_base.cpp:180`
- `socket_base_t::getsockopt`의 `ZMQ_FD` 처리: `src/socket_base.cpp:346`
- `socket_base_t::start_reaping`에서 reaper poller에 등록: `src/socket_base.cpp:1147`

### 검증 결론
- **단계 1에서 mailbox가 fd를 제거하면, 단계 2(io_thread/reaper 전환) 없이 즉시 빌드가 깨질 지점이 다수 존재합니다.**
- 특히 `ZMQ_FD` 옵션의 동작 정의(스레드 안전 소켓과의 호환성 포함)와 reaper/IO thread의 poller 등록 경로가 직접 영향을 받습니다.
- 따라서 Gemini의 지적처럼 **단계 1과 2는 결합되어야 하며**, 과도기 호환 계층(더미 fd, 조건부 폴러 경로 등) 없이 분리 진행하는 것은 리스크가 큽니다.

---

요약하면, 현재 코드베이스 기준으로 IPC는 POSIX에서 `stream_descriptor`에 강하게 의존하며, mailbox의 fd 기반 설계는 io_thread/reaper/소켓 옵션 경로에서 다수 활용됩니다. 계획대로 변경 시에는 Gemini의 지적 사항을 반드시 해결해야 합니다.
