# ASIO 완전 통합 해결 방법

## 핵심 발견

Windows IPC는 이미 `boost::asio::local::stream_protocol::socket`을 사용하고 있으며, `assign()` 메서드로 FD를 할당합니다:

```cpp
// Windows IPC (현재 구현)
_socket = new boost::asio::local::stream_protocol::socket(io_context);
_socket->assign(protocol, fd, ec);
```

POSIX는 같은 작업을 `stream_descriptor`로 수행합니다:

```cpp
// POSIX IPC (현재 구현)
_stream_descriptor = new boost::asio::posix::stream_descriptor(io_context, fd);
```

**Solution: POSIX도 Windows와 동일하게 `local::stream_protocol::socket::assign()`을 사용하면 됩니다!**

## 통합 전략

### 1. IPC Transport 통합

**모든 플랫폼에서 `local::stream_protocol::socket` 사용**

```cpp
// 통합된 ipc_transport.hpp
class ipc_transport_t {
private:
    std::unique_ptr<boost::asio::local::stream_protocol::socket> _socket;
    // stream_descriptor 완전 제거!
};

// 통합된 ipc_transport.cpp
bool ipc_transport_t::open(boost::asio::io_context &io_context, fd_t fd) {
    _socket = std::make_unique<boost::asio::local::stream_protocol::socket>(io_context);

    boost::asio::local::stream_protocol protocol;
    boost::system::error_code ec;
    _socket->assign(protocol, fd, ec);  // Windows와 POSIX 동일!

    return !ec;
}
```

**장점:**
- `#ifndef ZMQ_HAVE_WINDOWS` 완전 제거
- Windows와 POSIX 동일한 코드 경로
- `async_read_some`, `async_write_some` 통합

### 2. TCP Transport 통합

**모든 플랫폼에서 `tcp::socket` 사용**

```cpp
// 통합된 tcp_transport.hpp
class tcp_transport_t {
private:
    std::unique_ptr<boost::asio::ip::tcp::socket> _socket;
    // stream_descriptor 완전 제거!
};

// 통합된 tcp_transport.cpp
bool tcp_transport_t::open(boost::asio::io_context &io_context, fd_t fd) {
    // FD에서 protocol 추출 (AF_INET vs AF_INET6)
    boost::asio::ip::tcp protocol = protocol_for_fd(fd);

    _socket = std::make_unique<boost::asio::ip::tcp::socket>(io_context);

    boost::system::error_code ec;
    _socket->assign(protocol, fd, ec);  // POSIX에서도 assign 사용!

    return !ec;
}
```

**장점:**
- POSIX에서 `stream_descriptor` 제거
- Windows에서 WSAPoll 제거
- sendto/recvfrom 대신 socket 기반 async I/O
- syscall 감소 효과

### 3. Mailbox io_context::post() 전환

**모든 플랫폼에서 FD 없는 mailbox**

```cpp
// 통합된 mailbox.hpp
class mailbox_t {
private:
    boost::asio::io_context* _io_context;
    ypipe_t<command_t, command_pipe_granularity> _cpipe;
    std::atomic<bool> _scheduled;

    // signaler_t 제거!

public:
    void send(const command_t &cmd);
    void process_mailbox();  // io_context::post() 콜백
};

// 통합된 mailbox.cpp
void mailbox_t::send(const command_t &cmd) {
    _cpipe.write(cmd, false);
    bool ok = _cpipe.flush();

    if (!ok) {
        // Double-check 패턴으로 Lost Wakeup 방지
        if (!_scheduled.exchange(true, std::memory_order_acquire)) {
            boost::asio::post(*_io_context, [this]() {
                this->process_mailbox();
            });
        }
    }
}

void mailbox_t::process_mailbox() {
    do {
        // 큐 drain
        while (_cpipe.check_read()) {
            command_t cmd = _cpipe.read();
            cmd.destination->process_command(cmd);
        }

        // Double-check: scheduled = false 후 큐 재확인
        _scheduled.store(false, std::memory_order_release);

        if (!_cpipe.check_read())
            break;  // 큐가 비어있으면 종료

        // 큐가 남아있으면 재스케줄 시도
        if (_scheduled.exchange(true, std::memory_order_acquire))
            break;  // 이미 다른 스레드가 점유

        // 재점유 성공, 루프 계속
    } while (true);
}
```

**장점:**
- eventfd/socketpair 생성/파괴 오버헤드 제거
- write/read syscall 제거
- signaler FD 관리 불필요
- io_context 네이티브 wakeup 메커니즘 사용

### 4. Poller 통합

**소켓 전용 Poller로 단순화**

```cpp
// 통합된 asio_poller.hpp
class asio_poller_t {
private:
    boost::asio::io_context _io_context;

    struct poll_entry_t {
        // FD 제거!
        // Windows/POSIX 플랫폼 분기 제거!

        // 소켓 포인터만 유지 (variant 또는 다형성)
        std::variant<
            boost::asio::ip::tcp::socket*,
            boost::asio::local::stream_protocol::socket*
        > socket_ptr;

        i_poll_events *events;
        bool pollin_enabled;
        bool pollout_enabled;
    };

public:
    // add_fd(fd_t) 제거!
    // add_tcp_socket(), add_ipc_socket() 추가
    handle_t add_tcp_socket(boost::asio::ip::tcp::socket* socket, i_poll_events *events);
    handle_t add_ipc_socket(boost::asio::local::stream_protocol::socket* socket, i_poll_events *events);
};

// 통합된 asio_poller.cpp
void asio_poller_t::loop() {
    // WSAPoll 제거!
    // stream_descriptor 제거!

    // 순수 io_context::run_for() 기반
    _io_context.run_for(std::chrono::milliseconds(timeout));
}
```

**장점:**
- WSAPoll 완전 제거
- stream_descriptor 완전 제거
- FD 기반 등록 인터페이스 제거
- 소켓 타입별 async_wait 처리

## 구현 순서 (수정)

Gemini 피드백 반영:

### Phase 1: Mailbox + io_thread + reaper 통합 변경

**이유:** 단계를 분리하면 컴파일이 깨짐

**작업:**
1. `mailbox.hpp/cpp`: io_context::post() + Double-check 패턴
2. `io_thread.cpp`: `add_fd(mailbox.get_fd())` 제거, 순수 `io_context.run()` 기반
3. `reaper.cpp`: 동일
4. `socket_base.cpp`: `ZMQ_FD` 옵션 처리 수정 (비 thread-safe 소켓은 mailbox fd 반환 불가 명시)

**검증:**
- 기본 소켓 생성/종료 테스트
- mailbox 명령 처리 테스트

### Phase 2: IPC Transport 통합

**작업:**
1. `ipc_transport.hpp`: `#ifndef ZMQ_HAVE_WINDOWS` 제거, 단일 `_socket` 멤버
2. `ipc_transport.cpp`: POSIX 경로를 Windows와 동일하게 `assign()` 사용
3. `asio_ipc_connecter.cpp`, `asio_ipc_listener.cpp`: 변경 없음 (이미 `local::stream_protocol` 사용)

**검증:**
- IPC 연결/송수신 테스트
- test_ipc.cpp 전체 통과

### Phase 3: TCP Transport + Poller 통합

**작업:**
1. `tcp_transport.hpp/cpp`: POSIX에서 `stream_descriptor` 제거, `tcp::socket::assign()` 사용
2. `asio_poller.hpp`: `#ifndef ZMQ_HAVE_WINDOWS` 제거, variant 기반 socket 저장
3. `asio_poller.cpp`: WSAPoll 제거, 순수 async_wait + io_context::run_for()

**검증:**
- TCP 연결/송수신 테스트
- test_tcp.cpp, test_pair_tcp.cpp 전체 통과

### Phase 4: 플랫폼 #ifdef 완전 제거

**작업:**
1. 모든 `#ifdef ZMQ_HAVE_WINDOWS` 제거
2. 공통 코드 경로만 유지

**검증:**
- 전체 테스트 suite 통과 (64 tests)
- Windows/Linux/macOS 빌드 확인

## 예상 효과

### 성능 개선
- **read/write syscall 감소**: stream_descriptor → socket 전환으로 sendto/recvfrom 사용
- **mailbox overhead 제거**: eventfd/socketpair 생성/write/read 제거
- **epoll_wait 최적화**: 이미 88% 감소, 추가 개선 가능

### 코드 품질
- **~500 라인 제거**: 플랫폼 분기 코드
- **유지보수성 향상**: 단일 코드 경로
- **ASIO 네이티브**: 표준 비동기 패턴

### 리스크 완화
- **IPC 호환성**: `local::stream_protocol::socket::assign()` 사용으로 보존
- **Race Condition**: Double-check 패턴으로 방지
- **단계별 검증**: 통합 Phase로 컴파일 안정성 확보

## 참고 자료

- **현재 Windows IPC 구현**: `src/asio/ipc_transport.cpp:104-120`
- **ASIO local socket assign**: Boost.Asio 문서 (basic_stream_socket::assign)
- **TCP socket assign**: Boost.Asio 문서 (ip::tcp::socket::assign)
- **Double-check 패턴**: Gemini 리뷰 `review_gemini.md`
