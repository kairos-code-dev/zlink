# CppServer - Architecture & High-Performance Design Analysis

> **Author**: Ivan Shynkarenka (chronoxor)
> **License**: MIT License
> **Library**: CppServer (Asio-based C++ networking library)
> **Target**: High-Performance TCP/UDP/HTTP/WebSocket Server Application

---

## Source Reference Convention

본 문서의 모든 소스 참조는 아래 base path를 기준으로 한다.

```
BASE = core/tests/scenario/stream/cppserver/upstream
```

| 약칭 | 실제 경로 |
|------|----------|
| `asio/` | `{BASE}/include/server/asio/` |
| `http/` | `{BASE}/include/server/http/` |
| `ws/` | `{BASE}/include/server/ws/` |
| `src/asio/` | `{BASE}/source/server/asio/` |
| `src/http/` | `{BASE}/source/server/http/` |
| `src/ws/` | `{BASE}/source/server/ws/` |

---

## 1. Overview

CppServer는 **Asio(Think-Async) 라이브러리 기반의 고성능 C++ 네트워크 프레임워크**다.
핵심 설계 철학은 **IO-Service-Per-Thread 아키텍처 + Double-Buffered 비동기 송신 +
Handler Memory Pooling + Strand 기반 동기화**를 하나의 계층 구조로 통합해
높은 throughput과 낮은 latency를 동시에 달성하는 것이다.

```
┌──────────────────────────────────────────────────────────┐
│                  Application Layer                        │
│    (사용자 서브클래스: EchoSession, ChatServer 등)         │
│    onReceived() / onSent() / onConnected() 오버라이드     │
├──────────────────────────────────────────────────────────┤
│               Protocol Layer (HTTP / WebSocket)           │
│  ┌──────────────┐ ┌──────────────┐ ┌────────────────┐   │
│  │ HTTPServer   │ │ HTTPSession  │ │ FileCache      │   │
│  │ → http/      │ │ → http/      │ │ (정적 캐싱)    │   │
│  └──────────────┘ └──────────────┘ └────────────────┘   │
│  ┌──────────────┐ ┌──────────────┐                      │
│  │ WSServer     │ │ WSSession    │ (RFC 6455 프레임)     │
│  │ → ws/        │ │ → ws/        │                      │
│  └──────────────┘ └──────────────┘                      │
├──────────────────────────────────────────────────────────┤
│              TCP/UDP Transport Layer (Asio)               │
│  ┌──────────────┐ ┌──────────────┐ ┌────────────────┐   │
│  │ TCPServer    │ │ TCPSession   │ │ UDPServer      │   │
│  │ (Accept/     │ │ (Double-     │ │ (Connectionless│   │
│  │  Session Mgr)│ │  Buffer Send)│ │  Echo)         │   │
│  └──────────────┘ └──────────────┘ └────────────────┘   │
│  ┌──────────────┐ ┌──────────────┐                      │
│  │ SSLServer    │ │ SSLSession   │ (OpenSSL 래핑)        │
│  └──────────────┘ └──────────────┘                      │
├──────────────────────────────────────────────────────────┤
│                Asio Service (Event Loop)                  │
│  ┌────────────────────────────────────────────────────┐  │
│  │  Mode 1: IO-Service-Per-Thread (strand 불필요)     │  │
│  │  Mode 2: Thread-Pool (단일 io_service + strand)    │  │
│  │  Mode 3: Manual (외부 io_service 주입)             │  │
│  └────────────────────────────────────────────────────┘  │
│  + Round-Robin 부하 분산 (atomic index)                   │
│  + HandlerStorage (1KB 스택 메모리 풀)                    │
│  + Polling / Blocking 모드 선택                           │
├──────────────────────────────────────────────────────────┤
│          OS-Level I/O Multiplexing                        │
│    Linux: epoll  │  macOS: kqueue  │  Windows: IOCP      │
│                  Asio Reactor/Proactor                    │
└──────────────────────────────────────────────────────────┘
```

---

## 2. Directory Structure

```
cppserver/upstream/
├── include/server/
│   ├── asio/                         ← 핵심: 비동기 I/O 계층
│   │   ├── asio.h                    ← Asio 헤더 통합, InternetProtocol enum
│   │   ├── service.h                 ← IO 서비스 오케스트레이션
│   │   ├── memory.h                  ← Handler 메모리 풀 (1KB stack)
│   │   ├── tcp_server.h              ← TCP 서버 (Accept + Session 관리)
│   │   ├── tcp_session.h             ← TCP 세션 (Double-Buffer 송신)
│   │   ├── tcp_client.h              ← TCP 클라이언트
│   │   ├── tcp_resolver.h            ← DNS Resolver
│   │   ├── udp_server.h              ← UDP 서버
│   │   ├── udp_client.h              ← UDP 클라이언트
│   │   ├── udp_resolver.h            ← UDP DNS Resolver
│   │   ├── ssl_context.h             ← SSL/TLS 컨텍스트
│   │   ├── ssl_server.h              ← SSL 서버
│   │   ├── ssl_session.h             ← SSL 세션
│   │   ├── ssl_client.h              ← SSL 클라이언트
│   │   └── timer.h                   ← Asio 타이머
│   │
│   ├── http/                         ← HTTP/HTTPS 프로토콜 계층
│   │   ├── http_server.h             ← HTTP 서버 (TCPServer 상속)
│   │   ├── http_session.h            ← HTTP 세션 (TCPSession 상속)
│   │   ├── http_client.h             ← HTTP 클라이언트
│   │   ├── http_request.h            ← HTTP Request 파서
│   │   ├── http_response.h           ← HTTP Response 빌더
│   │   ├── https_server.h            ← HTTPS 서버
│   │   ├── https_session.h           ← HTTPS 세션
│   │   └── https_client.h            ← HTTPS 클라이언트
│   │
│   └── ws/                           ← WebSocket 프로토콜 계층
│       ├── ws.h                      ← WebSocket 프레임 인코딩/디코딩
│       ├── ws_server.h               ← WS 서버
│       ├── ws_session.h              ← WS 세션
│       ├── ws_client.h               ← WS 클라이언트
│       ├── wss_server.h              ← WSS(Secure) 서버
│       ├── wss_session.h             ← WSS 세션
│       └── wss_client.h              ← WSS 클라이언트
│
├── source/server/                    ← 구현 파일
│   ├── asio/                         ← 13개 .cpp (핵심 구현)
│   ├── http/                         ← 8개 .cpp
│   └── ws/                           ← 7개 .cpp
│
├── examples/                         ← 24+ 예제 프로그램
├── performance/                      ← 30+ 벤치마크 프로그램
├── modules/                          ← 의존성 (Asio, OpenSSL, CppCommon 등)
└── CMakeLists.txt                    ← 빌드 설정
```

---

## 3. Asio Service (이벤트 루프 오케스트레이션)

### 3.1 Service 클래스 개요

CppServer의 모든 네트워크 동작은 `Service` 클래스의 이벤트 루프 위에서 실행된다.
이 클래스가 전체 스레드 모델과 핸들러 디스패치 전략을 결정한다.

> **Source**: `asio/service.h:53-182`

```cpp
// service.h:53-54
class Service : public std::enable_shared_from_this<Service>
{
public:
    // Mode 1: IO-Service-Per-Thread (pool=false)
    // Mode 2: Thread-Pool (pool=true)
    explicit Service(int threads = 1, bool pool = false);      // L61

    // Mode 3: 외부 io_service 수동 주입
    explicit Service(const std::shared_ptr<asio::io_service>& service,
                     bool strands = false);                     // L67
    ...
};
```

### 3.2 세 가지 초기화 모드

> **Source**: `src/asio/service.cpp:16-47`

```cpp
// service.cpp:16-47
Service::Service(int threads, bool pool)
    : _strand_required(false), _polling(false),
      _started(false), _round_robin_index(0)
{
    if (threads == 0) {
        // 단일 IO 서비스, 스레드 없음 (수동 구동)
        _services.emplace_back(std::make_shared<asio::io_service>());
    }
    else if (!pool) {
        // ★ Mode 1: IO-Service-Per-Thread
        for (int thread = 0; thread < threads; ++thread) {
            _services.emplace_back(std::make_shared<asio::io_service>());  // L34
            _threads.emplace_back(std::thread());
        }
        // → 스레드마다 전용 io_service → strand 불필요!
    }
    else {
        // Mode 2: Thread-Pool
        _services.emplace_back(std::make_shared<asio::io_service>());      // L41
        for (int thread = 0; thread < threads; ++thread)
            _threads.emplace_back(std::thread());
        _strand = std::make_shared<asio::io_service::strand>(*_services[0]); // L44
        _strand_required = true;
        // → 단일 io_service를 여러 스레드가 공유 → strand로 직렬화 필수
    }
}
```

**핵심 차이**:

| | Mode 1: Per-Thread | Mode 2: Thread-Pool |
|--|---------------------|---------------------|
| io_service 수 | N개 (스레드 수만큼) | 1개 |
| strand 사용 | 불필요 | 필수 |
| 스레드 간 공유 상태 | 없음 | 있음 (strand 보호) |
| 코드 복잡도 | 단순 | 복잡 |
| **성능** | **최적 (lock-free)** | strand 오버헤드 존재 |

**Mode 1이 고성능의 핵심이다**: 각 스레드가 독립된 io_service를 가지므로
핸들러 실행 시 **어떤 동기화도 필요 없다**. 스레드 간 경합이 원천 차단된다.

### 3.3 Round-Robin 부하 분산

> **Source**: `asio/service.h:110-111`

```cpp
// service.h:110-111
virtual std::shared_ptr<asio::io_service>& GetAsioService() noexcept
{ return _services[++_round_robin_index % _services.size()]; }
```

새 세션이 생성될 때 `GetAsioService()`를 호출하여 다음 io_service를 선택한다.
`_round_robin_index`는 `std::atomic<size_t>` (`service.h:175`)이므로 **lock 없이** 원자적 증가한다.

```
Thread 0: io_service[0] ←── Session A, D, G, ...
Thread 1: io_service[1] ←── Session B, E, H, ...
Thread 2: io_service[2] ←── Session C, F, I, ...
         ↑
    Round-Robin으로 균등 배분
```

### 3.4 Dispatch vs Post (핸들러 실행 전략)

> **Source**: `asio/service.h:120-132`

```cpp
// service.h:121-122 - Dispatch: 같은 스레드면 즉시 실행, 아니면 큐잉
template <typename CompletionHandler>
Dispatch(handler)
{ if (_strand_required) return _strand->dispatch(handler);
  else return _services[0]->dispatch(handler); }

// service.h:131-132 - Post: 항상 큐에 넣어서 비동기 실행
template <typename CompletionHandler>
Post(handler)
{ if (_strand_required) return _strand->post(handler);
  else return _services[0]->post(handler); }
```

- **`dispatch()`**: 현재 이미 IO 스레드 안이면 핸들러를 **즉시 인라인 실행**. 컨텍스트 스위치 제로.
- **`post()`**: 무조건 IO 큐에 삽입. 다음 이벤트 루프 사이클에서 실행.

성능 관점에서 `dispatch`가 유리한 이유: hot path에서 불필요한 큐잉/스케줄링을 피한다.

### 3.5 ServiceThread (워커 스레드 메인 루프)

> **Source**: `src/asio/service.cpp:163-228`

```cpp
// service.cpp:163-228
void Service::ServiceThread(const std::shared_ptr<Service>& service,
                            const std::shared_ptr<asio::io_service>& io_service)
{
    bool polling = service->IsPolling();
    service->onThreadInitialize();                    // L168: 스레드 초기화 콜백

    try {
        asio::io_service::work work(*io_service);     // L173: IO 서비스 유지

        do {
            try {
                if (polling) {
                    io_service->poll();                // L184: 논블로킹 폴링
                    service->onIdle();                 // L187: idle 핸들러
                } else {
                    io_service->run();                 // L192: 블로킹 실행
                    break;
                }
            }
            catch (const asio::system_error& ex) {
                if (ec == asio::error::not_connected)  // L201: 연결 에러 스킵
                    continue;
                throw;
            }
        } while (service->IsStarted());               // L206
    }
    ...
    service->onThreadCleanup();                        // L222: 스레드 정리 콜백
}
```

**두 가지 실행 모드**:

| | Blocking (`run()`) | Polling (`poll()`) |
|--|--------------------|--------------------|
| 동작 | 핸들러 없으면 대기 (sleep) | 핸들러 없으면 즉시 리턴 |
| CPU 사용 | 유휴 시 0% | 유휴 시 높음 (busy-wait) |
| Latency | 이벤트 도착 시 wake-up 지연 | **최소 latency** |
| 용도 | 일반 서버 | 초저지연 트레이딩/게임 |

`onIdle()` 기본 구현은 `Thread::Yield()` (`service.h:152`)로, busy-wait 하되
다른 스레드에 CPU를 양보한다.

---

## 4. TCP Session (Double-Buffer 비동기 송신의 핵심)

### 4.1 TCPSession 클래스 구조

> **Source**: `asio/tcp_session.h:27-287`

```cpp
// tcp_session.h:234-264 - 핵심 멤버 변수
class TCPSession : public std::enable_shared_from_this<TCPSession>
{
private:
    CppCommon::UUID _id;                          // L236: 세션 고유 ID
    std::shared_ptr<TCPServer> _server;           // L238: 서버 참조
    std::shared_ptr<asio::io_service> _io_service;// L240: 전용 IO 서비스
    asio::io_service::strand _strand;             // L242: 핸들러 직렬화
    bool _strand_required;                        // L243

    asio::ip::tcp::socket _socket;                // L245: TCP 소켓
    std::atomic<bool> _connected;                 // L246: 연결 상태 (원자적)

    // 통계
    uint64_t _bytes_pending;                      // L248: 대기 바이트
    uint64_t _bytes_sending;                      // L249: 전송 중 바이트
    uint64_t _bytes_sent;                         // L250: 누적 전송 바이트
    uint64_t _bytes_received;                     // L251: 누적 수신 바이트

    // ★ 수신 버퍼
    bool _receiving;                              // L253: 수신 중 플래그
    size_t _receive_buffer_limit{0};              // L254: 수신 버퍼 상한
    std::vector<uint8_t> _receive_buffer;         // L255: 수신 버퍼
    HandlerStorage _receive_storage;              // L256: 수신 핸들러 메모리 풀

    // ★ 송신 더블 버퍼
    bool _sending;                                // L258: 전송 중 플래그
    std::mutex _send_lock;                        // L259: 송신 보호 뮤텍스
    size_t _send_buffer_limit{0};                 // L260: 송신 버퍼 상한
    std::vector<uint8_t> _send_buffer_main;       // L261: 축적 버퍼 (enqueue)
    std::vector<uint8_t> _send_buffer_flush;      // L262: 전송 버퍼 (flush)
    size_t _send_buffer_flush_offset;             // L263: 부분 전송 오프셋
    HandlerStorage _send_storage;                 // L264: 송신 핸들러 메모리 풀
};
```

### 4.2 Double-Buffer Send (고성능 송신의 핵심 원리)

이 패턴이 CppServer가 고성능을 달성하는 **가장 중요한 메커니즘**이다.

```
┌─ 송신 데이터 흐름 ──────────────────────────────────────────────┐
│                                                                 │
│  Thread A: SendAsync(data1) ──┐                                 │
│  Thread B: SendAsync(data2) ──┤──→ _send_buffer_main에 축적     │
│  Thread C: SendAsync(data3) ──┘    (std::mutex 보호, 짧은 임계) │
│                                         │                       │
│                               TrySend() │                       │
│                                         ▼                       │
│                          _send_buffer_main.swap(               │
│                              _send_buffer_flush)  ← O(1) swap  │
│                                         │                       │
│                                         ▼                       │
│                    async_write_some(flush_buffer)               │
│                         + offset tracking                       │
│                                         │                       │
│                              OS Kernel  │                       │
│                                         ▼                       │
│                         onSent() 콜백 → TrySend() 재호출        │
└─────────────────────────────────────────────────────────────────┘
```

#### 4.2.1 SendAsync (데이터 축적)

> **Source**: `src/asio/tcp_session.cpp:257-307`

```cpp
// tcp_session.cpp:257-307
bool TCPSession::SendAsync(const void* buffer, size_t size)
{
    if (!IsConnected()) return false;                   // L259
    if (size == 0) return true;                         // L262

    {
        std::scoped_lock locker(_send_lock);            // L270: ★ 짧은 임계 구간

        // 양쪽 버퍼 모두 비어있지 않으면 이미 핸들러가 동작 중
        bool send_required = _send_buffer_main.empty()
                          || _send_buffer_flush.empty(); // L273

        // 버퍼 상한 초과 검사
        if (((_send_buffer_main.size() + size) > _send_buffer_limit)
            && (_send_buffer_limit > 0))                 // L276
        {
            SendError(asio::error::no_buffer_space);
            return false;
        }

        // ★ main 버퍼에 데이터 추가 (vector::insert)
        const uint8_t* bytes = (const uint8_t*)buffer;
        _send_buffer_main.insert(
            _send_buffer_main.end(), bytes, bytes + size); // L284

        _bytes_pending = _send_buffer_main.size();       // L287

        // 이미 전송 핸들러가 동작 중이면 추가 dispatch 불필요
        if (!send_required) return true;                  // L290-291
    }
    // ← 여기서 lock 해제됨 (임계 구간 최소화!)

    // 전송 핸들러 디스패치
    auto self(this->shared_from_this());
    auto send_handler = [this, self]() { TrySend(); };
    if (_strand_required)
        _strand.dispatch(send_handler);                   // L302
    else
        _io_service->dispatch(send_handler);              // L304
    return true;
}
```

**핵심 설계 포인트**:
1. **Lock 범위 최소화**: `std::scoped_lock`의 범위가 `L270-L292`로 극도로 짧다. 데이터 복사(insert)만 하고 바로 해제.
2. **중복 핸들러 방지**: `send_required` 플래그로 불필요한 dispatch를 차단.
3. **데이터 축적 효과**: 여러 스레드의 SendAsync 호출이 main 버퍼에 쌓이고, 한 번의 시스템 콜로 전송.

#### 4.2.2 TrySend (버퍼 교체 + 비동기 전송)

> **Source**: `src/asio/tcp_session.cpp:487-563`

```cpp
// tcp_session.cpp:487-563
void TCPSession::TrySend()
{
    if (_sending) return;                               // L489: 이미 전송 중이면 리턴
    if (!IsConnected()) return;                         // L492

    // ★ 버퍼 Swap (핵심 최적화)
    if (_send_buffer_flush.empty())
    {
        std::scoped_lock locker(_send_lock);            // L498
        _send_buffer_flush.swap(_send_buffer_main);     // L501: O(1) 포인터 교환!
        _send_buffer_flush_offset = 0;                  // L502
        _bytes_pending = 0;                             // L505
        _bytes_sending += _send_buffer_flush.size();    // L506
    }

    if (_send_buffer_flush.empty()) {
        onEmpty();                                       // L513: 전송할 데이터 없음
        return;
    }

    // ★ 비동기 송신
    _sending = true;                                     // L518
    auto self(this->shared_from_this());
    auto async_write_handler = make_alloc_handler(       // L520: 핸들러 메모리 풀 사용!
        _send_storage,
        [this, self](std::error_code ec, size_t size)
    {
        _sending = false;                                // L522

        if (size > 0) {
            _bytes_sending -= size;                      // L531
            _bytes_sent += size;                         // L532
            _server->_bytes_sent += size;                // L533

            // ★ 부분 전송 처리 (offset tracking)
            _send_buffer_flush_offset += size;           // L536

            // 전체 flush 완료 시 버퍼 클리어
            if (_send_buffer_flush_offset == _send_buffer_flush.size()) {
                _send_buffer_flush.clear();              // L542
                _send_buffer_flush_offset = 0;           // L543
            }

            onSent(size, bytes_pending());               // L547
        }

        // ★ 재귀적 TrySend: 남은 데이터가 있으면 계속 전송
        if (!ec) TrySend();                              // L552
        else { SendError(ec); Disconnect(true); }
    });

    // flush 버퍼의 아직 전송 안 된 부분만 전송
    if (_strand_required)
        _socket.async_write_some(
            asio::buffer(_send_buffer_flush.data() + _send_buffer_flush_offset,
                         _send_buffer_flush.size() - _send_buffer_flush_offset),
            bind_executor(_strand, async_write_handler));  // L560
    else
        _socket.async_write_some(
            asio::buffer(_send_buffer_flush.data() + _send_buffer_flush_offset,
                         _send_buffer_flush.size() - _send_buffer_flush_offset),
            async_write_handler);                          // L562
}
```

**Double-Buffer의 동작 원리**:

```
시간 →

T1: SendAsync(A)  → main=[A]
T2: SendAsync(B)  → main=[A,B]
T3: TrySend()     → swap → flush=[A,B], main=[]
                   → async_write_some(flush)
T4: SendAsync(C)  → main=[C]          ← flush 전송 중에도 main에 축적 가능!
T5: SendAsync(D)  → main=[C,D]
T6: onSent(A,B)   → TrySend() 재호출
                   → swap → flush=[C,D], main=[]
                   → async_write_some(flush)
```

**핵심 이점**:
1. **Non-blocking Accumulation**: flush 버퍼가 OS에서 전송 중일 때도 main 버퍼에 데이터를 계속 축적 가능
2. **O(1) Swap**: `std::vector::swap()`은 내부 포인터만 교환하므로 O(1)
3. **Batch 효과**: 축적된 데이터를 한 번의 `async_write_some`으로 전송 → 시스템 콜 최소화
4. **Partial Write 처리**: `_send_buffer_flush_offset`으로 부분 전송 재개 (복사 없음)

### 4.3 TryReceive (적응형 수신 버퍼)

> **Source**: `src/asio/tcp_session.cpp:429-485`

```cpp
// tcp_session.cpp:429-485
void TCPSession::TryReceive()
{
    if (_receiving) return;                             // L431
    if (!IsConnected()) return;                         // L434

    _receiving = true;                                   // L438
    auto self(this->shared_from_this());

    // ★ HandlerStorage에서 핸들러 메모리 할당
    auto async_receive_handler = make_alloc_handler(
        _receive_storage,                                // L440: 1KB 스택 풀 사용!
        [this, self](std::error_code ec, size_t size)
    {
        _receiving = false;                              // L442

        if (size > 0) {
            _bytes_received += size;                     // L451
            _server->_bytes_received += size;            // L452

            // ★ 수신 데이터를 직접 전달 (Zero-Copy)
            onReceived(_receive_buffer.data(), size);    // L455

            // ★ 적응형 버퍼 크기 조정
            if (_receive_buffer.size() == size) {        // L458: 버퍼가 꽉 찼으면
                if (((2 * size) > _receive_buffer_limit)
                    && (_receive_buffer_limit > 0)) {
                    SendError(asio::error::no_buffer_space);
                    Disconnect(true);
                    return;
                }
                _receive_buffer.resize(2 * size);        // L468: 2배로 확장
            }
        }

        if (!ec) TryReceive();                           // L474: 계속 수신
        else { SendError(ec); Disconnect(true); }
    });

    // ★ strand 유무에 따른 비동기 읽기
    if (_strand_required)
        _socket.async_read_some(
            asio::buffer(_receive_buffer.data(), _receive_buffer.size()),
            bind_executor(_strand, async_receive_handler));  // L482
    else
        _socket.async_read_some(
            asio::buffer(_receive_buffer.data(), _receive_buffer.size()),
            async_receive_handler);                          // L484
}
```

**적응형 버퍼 리사이징**:

```
초기: receive_buffer = [    8KB    ]  (OS 소켓 버퍼 크기)
      ↓ 수신 8KB (꽉 참)
      → resize(16KB)
      ↓ 수신 16KB (꽉 참)
      → resize(32KB)
      ↓ 수신 12KB (여유 있음)
      → 유지 (32KB)

성장 전략: 2배 지수 성장 → amortized O(1), realloc 횟수 최소화
```

### 4.4 Connect (세션 초기화)

> **Source**: `src/asio/tcp_session.cpp:59-95`

```cpp
// tcp_session.cpp:59-95
void TCPSession::Connect()
{
    // ★ TCP_KEEPALIVE 설정
    if (_server->option_keep_alive())
        _socket.set_option(asio::ip::tcp::socket::keep_alive(true));  // L63

    // ★ TCP_NODELAY 설정 (Nagle 비활성화 → 저지연 핵심!)
    if (_server->option_no_delay())
        _socket.set_option(asio::ip::tcp::no_delay(true));            // L66

    // 수신/송신 버퍼 사전 할당
    _receive_buffer.resize(option_receive_buffer_size());              // L69
    _send_buffer_main.reserve(option_send_buffer_size());              // L70
    _send_buffer_flush.reserve(option_send_buffer_size());             // L71

    // 통계 초기화
    _bytes_pending = 0;
    _bytes_sending = 0;
    _bytes_sent = 0;
    _bytes_received = 0;                                               // L74-77

    _connected = true;                                                 // L80
    TryReceive();                                                      // L83: 즉시 수신 시작
    onConnected();                                                     // L86: 사용자 콜백
    ...
}
```

**TCP_NODELAY가 고성능에 미치는 영향**:

```
┌─ Nagle 알고리즘 ON (기본값) ────────────────────────┐
│                                                      │
│  send(10B) → "작은 패킷, 더 기다려보자..."            │
│  send(20B) → "아직 ACK 안 왔으니 버퍼링..."          │
│  (200ms 후 ACK 도착) → 30B를 하나로 합쳐 전송         │
│                                                      │
│  결과: 처리량은 좋지만 지연시간 200ms+ 추가           │
└──────────────────────────────────────────────────────┘

┌─ TCP_NODELAY (Nagle OFF) ───────────────────────────┐
│                                                      │
│  send(10B) → 즉시 전송!                              │
│  send(20B) → 즉시 전송!                              │
│                                                      │
│  결과: 패킷 수는 늘지만 지연시간 최소화               │
│  → Echo 서버, 게임, 실시간 시스템에 필수              │
└──────────────────────────────────────────────────────┘
```

---

## 5. Handler Memory Management (핸들러 메모리 풀링)

### 5.1 HandlerStorage (1KB 스택 버퍼)

> **Source**: `asio/memory.h:26-54`

```cpp
// memory.h:26-54
class HandlerStorage
{
public:
    HandlerStorage() noexcept : _in_use(false) {}         // L29

    void* allocate(size_t size);                           // L42
    void deallocate(void* ptr);                            // L47

private:
    bool _in_use;                                          // L51: 사용 중 플래그
    std::byte _storage[1024];                              // L53: ★ 1KB 고정 메모리!
};
```

**동작 원리**:

```
allocate(size):
  if (!_in_use && size < sizeof(_storage)):   ← size < 1024 (미만, 이하 아님!)
    _in_use = true
    return &_storage        ← 힙 할당 없이 스택 메모리 반환!
  else:
    return ::operator new(size)  ← fallback: 전역 할당

deallocate(ptr):
  if (ptr == &_storage):
    _in_use = false         ← 플래그만 리셋 (free 없음!)
  else:
    ::operator delete(ptr)
```

### 5.2 AllocateHandler (Asio 커스텀 할당 통합)

> **Source**: `asio/memory.h:131-169`

```cpp
// memory.h:131-161
template <typename THandler>
class AllocateHandler {
public:
    typedef HandlerAllocator<THandler> allocator_type;

    AllocateHandler(HandlerStorage& storage, THandler handler) noexcept
        : _storage(storage), _handler(handler) {}          // L143

    allocator_type get_allocator() const noexcept
    { return allocator_type(_storage); }                    // L152

    template <typename ...Args>
    void operator()(Args&&... args)
    { _handler(std::forward<Args>(args)...); }              // L156
};

// 편의 팩토리 함수 (L168-169)
template <typename THandler>
AllocateHandler<THandler> make_alloc_handler(HandlerStorage& storage, THandler handler);
```

**사용 위치**:

각 TCPSession은 **두 개의 HandlerStorage**를 보유한다 (`tcp_session.h:256,264`):
- `_receive_storage`: 수신 핸들러 메모리 (TryReceive에서 사용)
- `_send_storage`: 송신 핸들러 메모리 (TrySend에서 사용)

**왜 이것이 성능에 중요한가**:

```
┌─ 일반 Asio 핸들러 (매번 힙 할당) ──────────────────┐
│                                                     │
│  async_read → new handler (malloc)                  │
│  completion → delete handler (free)                 │
│  async_read → new handler (malloc)  ← 반복!        │
│  completion → delete handler (free)                 │
│                                                     │
│  문제: 고빈도 I/O에서 malloc/free가 병목             │
│  → 메모리 프래그먼테이션, 캐시 미스                  │
└─────────────────────────────────────────────────────┘

┌─ CppServer HandlerStorage (스택 풀) ───────────────┐
│                                                     │
│  async_read → allocate from _storage (즉시!)        │
│  completion → deallocate: _in_use = false            │
│  async_read → allocate from _storage (재사용!)       │
│  completion → deallocate: _in_use = false            │
│                                                     │
│  효과: malloc/free 0회, 캐시 친화적                  │
│  → Hot path에서 힙 할당 완전 제거                    │
└─────────────────────────────────────────────────────┘
```

Asio 핸들러는 보통 크기가 작다 (람다 캡처 + 함수 포인터 ≈ 수십~수백 바이트).
1024바이트 스택 버퍼로 대부분의 핸들러를 커버할 수 있다.

---

## 6. TCP Server (세션 라이프사이클 관리)

### 6.1 TCPServer 클래스 구조

> **Source**: `asio/tcp_server.h:30-259`

```cpp
// tcp_server.h:209-241 - 핵심 멤버 변수
class TCPServer : public std::enable_shared_from_this<TCPServer>
{
protected:
    std::shared_mutex _sessions_lock;                      // L211: Reader-Writer Lock
    std::map<CppCommon::UUID, std::shared_ptr<TCPSession>> _sessions; // L212

private:
    CppCommon::UUID _id;                                   // L216
    std::shared_ptr<Service> _service;                     // L218
    std::shared_ptr<asio::io_service> _io_service;         // L220
    asio::io_service::strand _strand;                      // L222
    bool _strand_required;                                 // L223

    asio::ip::tcp::acceptor _acceptor;                     // L230
    std::atomic<bool> _started;                            // L231
    HandlerStorage _acceptor_storage;                      // L232: Accept 핸들러 메모리 풀

    // 서버 전체 통계
    uint64_t _bytes_pending;                               // L234
    uint64_t _bytes_sent;                                  // L235
    uint64_t _bytes_received;                              // L236

    // 소켓 옵션
    bool _option_keep_alive;                               // L238
    bool _option_no_delay;                                 // L239
    bool _option_reuse_address;                            // L240
    bool _option_reuse_port;                               // L241
};
```

### 6.2 Start (서버 시작 + 소켓 옵션)

> **Source**: `src/asio/tcp_server.cpp:97-145`

```cpp
// tcp_server.cpp:97-145
bool TCPServer::Start()
{
    auto self(this->shared_from_this());
    auto start_handler = [this, self]()
    {
        // Acceptor 생성 및 설정
        _acceptor = asio::ip::tcp::acceptor(*_io_service);
        _acceptor.open(_endpoint.protocol());

        // ★ SO_REUSEADDR: 서버 재시작 시 즉시 바인드
        if (option_reuse_address())
            _acceptor.set_option(
                asio::ip::tcp::acceptor::reuse_address(true));   // L114

        // ★ SO_REUSEPORT: 커널 레벨 로드 밸런싱 (Linux/macOS)
#if (defined(unix) || defined(__unix) || defined(__unix__) || defined(__APPLE__))
        if (option_reuse_port()) {
            typedef asio::detail::socket_option::boolean<
                SOL_SOCKET, SO_REUSEPORT> reuse_port;
            _acceptor.set_option(reuse_port(true));              // L119
        }
#endif
        _acceptor.bind(_endpoint);                               // L122
        _acceptor.listen();                                      // L123

        _started = true;                                         // L131
        onStarted();
        Accept();                                                // L137: 첫 Accept 시작
    };
    ...
}
```

**SO_REUSEPORT의 성능 의미**:

```
┌─ 일반 (SO_REUSEPORT 없음) ───────────────────────┐
│                                                    │
│  단일 Acceptor ──→ 모든 연결을 하나의 스레드가 처리  │
│                    (Accept 병목 가능)               │
└────────────────────────────────────────────────────┘

┌─ SO_REUSEPORT 활성화 ────────────────────────────┐
│                                                    │
│  Process/Thread A: Acceptor on port 8080           │
│  Process/Thread B: Acceptor on port 8080           │
│  Process/Thread C: Acceptor on port 8080           │
│                                                    │
│  커널이 자동으로 연결을 분배 → Accept 병목 해소     │
└────────────────────────────────────────────────────┘
```

### 6.3 Accept (비동기 연결 수락 루프)

> **Source**: `src/asio/tcp_server.cpp:197-236`

```cpp
// tcp_server.cpp:197-236
void TCPServer::Accept()
{
    if (!IsStarted()) return;

    auto self(this->shared_from_this());

    // ★ HandlerStorage로 Accept 핸들러 메모리 풀링
    auto accept_handler = make_alloc_handler(
        _acceptor_storage,                                    // L204
        [this, self]()
    {
        // 세션 팩토리 메서드로 새 세션 생성
        _session = CreateSession(self);                       // L210

        // ★ 중첩된 비동기 Accept
        auto async_accept_handler = make_alloc_handler(
            _acceptor_storage,                                // L212
            [this, self](std::error_code ec)
        {
            if (!ec) {
                RegisterSession();                             // L216
                _session->Connect();                           // L219
            } else
                SendError(ec);

            Accept();                                          // L225: 다음 Accept 즉시 시작!
        });

        if (_strand_required)
            _acceptor.async_accept(
                _session->socket(),
                bind_executor(_strand, async_accept_handler)); // L228
        else
            _acceptor.async_accept(
                _session->socket(), async_accept_handler);     // L230
    });
    ...
}
```

**Accept 루프 패턴**:

```
Accept() ──→ CreateSession() ──→ async_accept()
                                      │
                              완료 콜백│
                                      ▼
                              RegisterSession()
                              _session->Connect()
                              Accept() ←─── 즉시 다음 Accept (재귀적 체인)
```

이 체인은 서버가 Stop될 때까지 무한 반복되어, 항상 하나의 Accept 요청이 대기 상태를 유지한다.

### 6.4 Session 관리 (Reader-Writer Lock)

> **Source**: `src/asio/tcp_server.cpp:294-313`

```cpp
// 세션 등록: Exclusive Lock (쓰기)
void TCPServer::RegisterSession() {
    std::unique_lock<std::shared_mutex> locker(_sessions_lock);  // L296
    _sessions.emplace(_session->id(), _session);                 // L299
}

// 세션 해제: Exclusive Lock (쓰기)
void TCPServer::UnregisterSession(const CppCommon::UUID& id) {
    std::unique_lock<std::shared_mutex> locker(_sessions_lock);  // L304
    auto it = _sessions.find(id);
    if (it != _sessions.end())
        _sessions.erase(it);                                     // L311
}

// Multicast: Shared Lock (읽기)
bool TCPServer::Multicast(const void* buffer, size_t size) {
    std::shared_lock<std::shared_mutex> locker(_sessions_lock);  // L250
    for (auto& session : _sessions)
        session.second->SendAsync(buffer, size);                 // L254
}

// 세션 조회: Shared Lock (읽기)
std::shared_ptr<TCPSession> TCPServer::FindSession(const CppCommon::UUID& id) {
    std::shared_lock<std::shared_mutex> locker(_sessions_lock);  // L287
    auto it = _sessions.find(id);
    return (it != _sessions.end()) ? it->second : nullptr;       // L291
}
```

**`std::shared_mutex` 사용의 이점**:

| 연산 | Lock 타입 | 동시 접근 |
|------|----------|----------|
| `RegisterSession()` | `unique_lock` (배타적) | 단독 |
| `UnregisterSession()` | `unique_lock` (배타적) | 단독 |
| `Multicast()` | `shared_lock` (공유) | 여러 읽기 동시 가능 |
| `FindSession()` | `shared_lock` (공유) | 여러 읽기 동시 가능 |

세션 등록/해제보다 Multicast/조회가 훨씬 빈번하므로, Reader-Writer Lock이 효과적이다.

---

## 7. Protocol Layers (HTTP / WebSocket)

### 7.1 HTTP Layer

HTTP 계층은 TCP 계층을 **상속**으로 확장한다.

```
HTTPServer  ──extends──→  TCPServer
    └── CreateSession() 오버라이드

HTTPSession ──extends──→  TCPSession
    └── onReceived() 오버라이드 → HTTP Request 파싱
    └── FileCache& 참조 (정적 파일 캐싱)
```

**FileCache**: 정적 파일을 메모리에 캐싱하여 디스크 I/O를 제거한다.
Watchdog 메커니즘으로 TTL(기본 1시간) 기반 캐시 갱신.

### 7.2 WebSocket Layer

WebSocket 계층은 HTTP 위에 **다중 상속**으로 구현된다:

> **Source**: `ws/ws_server.h:28`, `http/http_server.h:29`

```cpp
// ws_server.h:28 - HTTPServer + WebSocket 다중 상속
class WSServer : public HTTP::HTTPServer, protected WebSocket { ... };

// http_server.h:29 - TCPServer 단일 상속
class HTTPServer : public Asio::TCPServer { ... };
```

```
WSServer  ──extends──→  HTTPServer ──extends──→  TCPServer
    │
    └── protected WebSocket   ← WS 프레임 인코딩/디코딩 기능 (다중 상속)

WSSession ──extends──→  HTTPSession ──extends──→  TCPSession
                         └── HTTP 101 Upgrade 핸들링
                         └── WS 프레임 인코딩/디코딩
```

**프레임 처리 핵심** (`ws/ws.h`):
- `PrepareSendFrame()`: FIN 비트 + opcode + 마스킹 + 길이 인코딩
- `PrepareReceiveFrame()`: 인바운드 프레임 디코딩
- 멀티캐스트: `_ws_send_lock` mutex 보호 하에 모든 세션에 브로드캐스트 (`ws_server.h:50-59`)

### 7.3 SSL/TLS Layer

SSL 계층은 TCPServer를 상속하지 **않고**, 동일한 패턴으로 **독립 구현**되어 있다.

> **Source**: `asio/ssl_server.h:31`

```cpp
// ssl_server.h:31 - TCPServer와 별개의 독립 클래스
class SSLServer : public std::enable_shared_from_this<SSLServer>
{
    // TCPServer와 동일한 멤버 구성:
    // _service, _io_service, _strand, _sessions_lock, _sessions,
    // _acceptor, _acceptor_storage, _bytes_pending/sent/received ...
    // + 추가: std::shared_ptr<SSLContext> _context   ← SSL 전용
};
```

```
SSLServer  ──(독립 구현)──  TCPServer와 동일 패턴 + SSLContext
SSLSession ──(독립 구현)──  TCPSession와 동일 패턴 + OpenSSL 스트림 래핑
SSLContext ──────────────→  인증서 체인, 개인키, DH 파라미터 관리
```

**TCPServer를 상속하지 않는 이유**: SSL 스트림(`asio::ssl::stream`)은 일반 소켓과
타입이 다르기 때문에, 템플릿이 아닌 별도 클래스로 구현된다. 대신 동일한 설계 패턴
(Double-Buffer Send, HandlerStorage, Strand, shared_mutex 세션 관리)을 그대로 복제한다.

---

## 8. Performance Optimization Summary

### 8.1 고성능을 만드는 핵심 기법 정리

| 기법 | 소스 위치 | 효과 |
|------|----------|------|
| **IO-Service-Per-Thread** | `src/asio/service.cpp:29-36` | 스레드 간 경합 원천 차단, strand 오버헤드 제거 |
| **Double-Buffer Send** | `src/asio/tcp_session.cpp:487-563` | 축적 + O(1) swap + 한 번의 시스템 콜로 배치 전송 |
| **Handler Memory Pool** | `asio/memory.h:26-54` | Hot path에서 malloc/free 완전 제거 (1KB 스택) |
| **Round-Robin 분배** | `asio/service.h:110-111` | 세션을 IO 스레드에 균등 배분 (atomic, lock-free) |
| **TCP_NODELAY** | `src/asio/tcp_session.cpp:65-66` | Nagle 비활성화 → 즉시 전송, 저지연 |
| **SO_REUSEPORT** | `src/asio/tcp_server.cpp:116-120` | 커널 레벨 Accept 부하 분산 (Linux) |
| **Dispatch (즉시 실행)** | `asio/service.h:121-122` | IO 스레드 내에서 큐잉 없이 인라인 실행 |
| **적응형 수신 버퍼** | `src/asio/tcp_session.cpp:458-468` | 2배 지수 성장, realloc 최소화 |
| **Shared Lock** | `src/asio/tcp_server.cpp:250,287` | 읽기 연산 동시성 극대화 |
| **Minimal Lock Scope** | `src/asio/tcp_session.cpp:270-292` | Lock 범위를 데이터 복사만으로 한정 |
| **Partial Write Offset** | `src/asio/tcp_session.cpp:536` | 부분 전송 시 복사 없이 오프셋만 이동 |
| **Virtual Callback** | `asio/tcp_session.h:191-232` | Zero-cost 확장 (인라인 가능한 빈 기본 구현) |

### 8.2 성능 흐름 (Echo 서버 메시지 라이프사이클)

```
[클라이언트 → 서버 수신]
  ① OS가 데이터 도착 통보 (epoll/kqueue/IOCP)
  ② io_service가 TryReceive 핸들러 실행
     → async_read_some 완료 콜백 (HandlerStorage에서 할당)
  ③ onReceived(buffer, size) 호출
     → 수신 버퍼를 직접 전달 (중간 복사 없음)

[서버 → 클라이언트 송신 (Echo)]
  ④ onReceived 내에서 SendAsync(buffer, size) 호출
  ⑤ std::scoped_lock(_send_lock) 진입
     → _send_buffer_main에 데이터 insert
     → lock 해제 (수 마이크로초)
  ⑥ TrySend() dispatch
     → _send_buffer_main ↔ _send_buffer_flush swap (O(1))
     → async_write_some(flush_buffer)
     → 핸들러는 HandlerStorage에서 할당 (malloc 0회)
  ⑦ OS가 전송 완료 통보
     → onSent() 콜백
     → _send_buffer_flush 클리어 (또는 offset 전진)
     → TrySend() 재호출 (남은 데이터 처리)
```

### 8.3 아키텍처 설계 비교

```
┌──────────────────┬──────────────┬──────────┬──────────┬──────────────┐
│                  │ CppServer    │ Boost.   │ CGDK10   │ 일반 TCP     │
│                  │              │ Asio     │          │ (교과서적)   │
├──────────────────┼──────────────┼──────────┼──────────┼──────────────┤
│ I/O 모델         │ Asio 기반    │ Asio     │ 직접     │ select/poll  │
│                  │ epoll/IOCP   │ 동일     │ IOCP/RIO │              │
│                  │              │          │ /epoll   │              │
├──────────────────┼──────────────┼──────────┼──────────┼──────────────┤
│ 스레드 모델      │ Per-Thread   │ 수동     │ 스레드풀 │ 스레드-per-  │
│                  │ io_service   │ 구성     │          │ connection   │
├──────────────────┼──────────────┼──────────┼──────────┼──────────────┤
│ 송신 버퍼링      │ Double-      │ 없음     │ Scatter- │ 단일 버퍼    │
│                  │ Buffer Swap  │ (수동)   │ Gather   │              │
├──────────────────┼──────────────┼──────────┼──────────┼──────────────┤
│ 핸들러 메모리    │ 1KB Stack    │ 없음     │ Object   │ 없음         │
│                  │ Pool         │ (힙)     │ Pool     │ (힙)         │
├──────────────────┼──────────────┼──────────┼──────────┼──────────────┤
│ 직렬화           │ 없음 (raw)   │ 없음     │ Zero-    │ JSON 등      │
│                  │              │          │ Copy     │              │
├──────────────────┼──────────────┼──────────┼──────────┼──────────────┤
│ 동기화           │ Minimal Lock │ Strand   │ Lock-    │ Mutex        │
│                  │ + Strand     │          │ Free CAS │ 전역         │
├──────────────────┼──────────────┼──────────┼──────────┼──────────────┤
│ Lock-Free 자료   │ Atomic RR    │ N/A      │ CAS      │ 없음         │
│ 구조             │ index        │          │ Stack    │              │
├──────────────────┼──────────────┼──────────┼──────────┼──────────────┤
│ 프로토콜 지원    │ TCP/UDP/     │ TCP/UDP  │ TCP      │ TCP          │
│                  │ HTTP/WS/SSL  │          │          │              │
└──────────────────┴──────────────┴──────────┴──────────┴──────────────┘
```

**CGDK10 대비 CppServer의 특징**:
- CGDK10은 OS API를 **직접** 호출 (IOCP/RIO/epoll), CppServer는 **Asio 추상화** 사용
- CGDK10은 **Zero-Copy 직렬화** 내장, CppServer는 직렬화 없이 raw 바이트 전달
- CGDK10은 **Lock-Free CAS Stack** 사용, CppServer는 **Strand + Minimal Mutex** 사용
- CppServer는 **프로토콜 계층이 풍부** (HTTP, WebSocket, SSL 기본 제공)
- CppServer는 Asio 생태계와 호환되어 **이식성**이 더 높음

---

## 9. Design Principles

### 9.1 핵심 설계 철학

1. **"시스템 콜을 줄여라"**: Double-Buffer Swap으로 다수의 SendAsync를 하나의 `async_write_some`으로 통합.
   - `_send_buffer_main`에 축적 → O(1) swap → 단일 쓰기 시스템 콜
   - `src/asio/tcp_session.cpp:501,560-562`

2. **"잠금 범위를 최소화하라"**: `std::scoped_lock`을 **데이터 복사 구간에만** 사용.
   - SendAsync의 lock 범위: `L270-L292` (약 20줄)
   - 비동기 디스패치는 lock 밖에서 수행 (`L301-L304`)

3. **"힙 할당을 피하라"**: 1KB `HandlerStorage`로 비동기 핸들러의 heap 할당을 제거.
   - 세션당 2개 (receive + send): `tcp_session.h:256,264`
   - 서버당 1개 (acceptor): `tcp_server.h:232`
   - 총 handler 할당 중 99%+ 가 스택 풀에서 충족

4. **"스레드를 격리하라"**: IO-Service-Per-Thread 모드에서 스레드 간 공유 상태가 없다.
   - 각 세션은 생성 시 하나의 io_service에 바인딩 (`tcp_session.cpp:18`)
   - 같은 io_service의 핸들러는 항상 같은 스레드에서 실행
   - 결과: 대부분의 연산이 lock-free

5. **"확장은 가상 함수로"**: `onReceived()`, `onSent()`, `onConnected()` 등 빈 기본 구현.
   - `tcp_session.h:191-224`
   - 사용자는 서브클래스로 원하는 동작만 오버라이드
   - 빈 가상 함수는 컴파일러가 최적화로 제거 가능

6. **"Asio 생태계를 활용하라"**: OS별 I/O 모델(epoll/kqueue/IOCP)은 Asio에 위임.
   - 크로스 플랫폼 이식성 확보
   - Asio의 성숙한 최적화 (reactor/proactor 패턴) 활용
   - OpenSSL, HTTP 파싱 등도 검증된 라이브러리 활용

### 9.2 사용된 외부 의존성

| 라이브러리 | 용도 | 비고 |
|-----------|------|------|
| **Asio (Think-Async)** | 비동기 I/O 이벤트 루프 | 핵심 의존성 (Boost 불필요, standalone) |
| **OpenSSL** | SSL/TLS 암호화 | SSL 계층에서 사용 |
| **CppCommon** | UUID, Thread, Timespan 유틸 | 보조 유틸리티 |
| C++ Standard Library | `std::vector`, `std::map`, `std::mutex`, `std::atomic` | 필수 |

---

## 10. Conclusion

CppServer의 고성능은 **세 가지 핵심 최적화의 조합**에서 나온다.

```
성능 = IO-Service-Per-Thread     (service.h:61, service.cpp:29-36)
          → 스레드 간 경합 제거

     + Double-Buffer Send        (tcp_session.cpp:487-563)
          → 시스템 콜 최소화 + Non-blocking 축적

     + Handler Memory Pool       (memory.h:26-54)
          → Hot path 힙 할당 제거
```

이 위에 **TCP_NODELAY**, **SO_REUSEPORT**, **적응형 버퍼**, **Reader-Writer Lock**,
**dispatch 즉시 실행** 등의 세부 최적화가 더해져 전체적인 성능을 끌어올린다.

CGDK10과 비교하면 CppServer는 **Asio 추상화 레이어를 사용**하므로 극한의 저수준 최적화
(Zero-Copy 직렬화, Lock-Free CAS Stack, Scatter-Gather I/O)는 없지만,
**풍부한 프로토콜 지원**(HTTP/WebSocket/SSL)과 **크로스 플랫폼 이식성**,
그리고 **깔끔한 API 설계**로 실용적인 고성능 서버 개발에 적합하다.

```
┌──────────────────────────────────────────────────────┐
│  고성능의 핵심 = "불필요한 것을 하지 않는 것"         │
│                                                      │
│  • 불필요한 복사를 하지 않는다 (Double-Buffer Swap)   │
│  • 불필요한 할당을 하지 않는다 (HandlerStorage)       │
│  • 불필요한 잠금을 하지 않는다 (Per-Thread IO)        │
│  • 불필요한 시스템 콜을 하지 않는다 (배치 전송)       │
│  • 불필요한 대기를 하지 않는다 (dispatch 즉시 실행)   │
└──────────────────────────────────────────────────────┘
```

---

## Appendix: Quick Source Reference

| 컴포넌트 | 파일 | 핵심 라인 | 설명 |
|---------|------|----------|------|
| Service 클래스 | `asio/service.h` | L53-182 | IO 서비스 오케스트레이션 |
| Per-Thread 초기화 | `src/asio/service.cpp` | L29-36 | io_service × N 생성 |
| Thread-Pool 초기화 | `src/asio/service.cpp` | L38-46 | 단일 io_service + strand |
| Round-Robin 분배 | `asio/service.h` | L110-111 | atomic index % size |
| ServiceThread 루프 | `src/asio/service.cpp` | L163-228 | polling / blocking 모드 |
| Dispatch/Post | `asio/service.h` | L121-132 | 핸들러 실행 전략 |
| HandlerStorage | `asio/memory.h` | L26-54 | 1KB 스택 메모리 풀 |
| AllocateHandler | `asio/memory.h` | L131-169 | Asio 커스텀 할당 래퍼 |
| TCPSession 구조 | `asio/tcp_session.h` | L27-287 | 세션 전체 정의 |
| Double-Buffer 멤버 | `asio/tcp_session.h` | L257-264 | main/flush/offset/lock |
| SendAsync | `src/asio/tcp_session.cpp` | L257-307 | 데이터 축적 + 핸들러 dispatch |
| TrySend | `src/asio/tcp_session.cpp` | L487-563 | 버퍼 swap + async_write_some |
| TryReceive | `src/asio/tcp_session.cpp` | L429-485 | 적응형 수신 + 핸들러 풀 |
| Connect (소켓 옵션) | `src/asio/tcp_session.cpp` | L59-95 | KEEPALIVE, NODELAY |
| TCPServer 구조 | `asio/tcp_server.h` | L30-259 | 서버 전체 정의 |
| Server Start | `src/asio/tcp_server.cpp` | L97-145 | REUSEADDR, REUSEPORT |
| Accept 루프 | `src/asio/tcp_server.cpp` | L197-236 | 비동기 연결 수락 체인 |
| RegisterSession | `src/asio/tcp_server.cpp` | L294-300 | unique_lock (배타적) |
| Multicast | `src/asio/tcp_server.cpp` | L238-257 | shared_lock (공유) |
| ClearBuffers | `src/asio/tcp_session.cpp` | L565-579 | 송수신 버퍼 정리 |
