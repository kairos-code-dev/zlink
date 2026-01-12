# ZLink 아키텍처 문서

## 1. 개요 (Overview)

**ZLink**는 기존 `libzmq` (ZeroMQ v4.3.5)를 기반으로 불필요한 기능을 제거하고, 핵심 I/O 처리부를 **Boost.Asio** 기반의 **Proactor 패턴**으로 재설계한 경량화된 고성능 메시징 라이브러리입니다.

### 주요 특징
*   **Minimalism**: 필수 소켓(STREAM, PAIR, ROUTER, DEALER, PUB, SUB)만 지원하며, 암호화(Sodium, Curve) 및 불필요한 레거시 API를 제거했습니다.
*   **ASIO-based Proactor**: 기존의 `epoll`/`kqueue` 기반 Reactor 패턴을 **Boost.Asio**의 `io_context`를 활용한 완전한 비동기 Proactor 패턴으로 대체했습니다.
*   **WebSockets**: 기본적으로 WebSocket(WS/WSS) 전송 계층을 지원합니다.
*   **Cross-Platform**: Linux, macOS, Windows (x64/ARM64)를 단일한 ASIO 추상화 계층으로 지원합니다.

---

## 2. 시스템 아키텍처 (System Architecture)

ZLink는 크게 **User API**, **Core Logic**, **ASIO Engine**, **OS I/O**의 4계층으로 구성됩니다.

```mermaid
graph TD
    subgraph "User Application"
        API[ZMQ API (zmq_send/recv)]
    end

    subgraph "Core Layer (Legacy Compatible)"
        Socket[Socket Base<br/>(router, dealer, etc.)]
        Session[Session Base]
        Ctx[Context (ctx_t)]
    end

    subgraph "ASIO Proactor Layer (New)"
        IOThread[IO Thread<br/>(holds io_context)]
        Poller[ASIO Poller<br/>(Bridge)]
        Engine[ASIO Engine<br/>(Proactor Logic)]
        Transport[Transport<br/>(TCP / WS)]
    end

    subgraph "OS Layer"
        Kernel[OS Kernel<br/>(TCP/IP Stack)]
    end

    API --> Socket
    Socket --> Session
    Session --> Engine
    Ctx --> IOThread
    IOThread --> Poller
    Poller --> Engine
    Engine --> Transport
    Transport --> Kernel
```

---

## 3. 핵심 컴포넌트 (Key Components)

### 3.1 ASIO Poller (`src/asio/asio_poller.hpp`)
기존 ZMQ의 `poller` 인터페이스를 유지하면서, 내부적으로는 `boost::asio::io_context`를 래핑합니다.
*   **역할**: 레거시 코드가 `poller`라고 인식하게 하면서, 실제로는 ASIO의 이벤트 루프(`io_context::run()`)를 구동합니다.
*   **특징**: `epoll_wait` 대신 ASIO 핸들러를 통해 이벤트를 처리합니다.

### 3.2 ASIO Engine (`src/asio/asio_engine.hpp`)
**"True Proactor Mode"**의 핵심 구현체입니다.
*   **동작 방식**: 소켓의 준비 상태(Readiness)를 기다리지 않고, `async_read` / `async_write`를 직접 호출하여 완료 시 콜백(Handler)을 받습니다.
*   **Zero-Copy 지향**: 내부 버퍼링을 최소화하고 스트림 처리를 최적화했습니다.

### 3.3 Transports (`src/asio/*_transport.hpp`)
물리적 연결을 추상화합니다.
*   **`tcp_transport`**: Boost.Asio TCP 소켓을 래핑.
*   **`ws_transport`**: Boost.Beast 등을 활용할 수 있는 구조로, WebSocket 프로토콜 처리.

### 3.4 IO Thread (`src/io_thread.cpp`)
각 I/O 스레드는 하나의 `asio_poller` (즉, 하나의 `io_context`)를 가집니다. 이를 통해 멀티 코어 환경에서 부하를 분산합니다.

---

## 4. 데이터 흐름 (Data Flow)

### 메시지 전송 (Send Path)
1.  **User**: `zmq_send()` 호출.
2.  **Socket**: 해당 소켓 타입(예: DEALER)의 로드 밸런싱 로직에 따라 파이프(Pipe) 선택.
3.  **Session**: 파이프에서 메시지를 꺼내 `Engine`으로 전달.
4.  **ASIO Engine**: 메시지를 `async_write`로 커널 버퍼에 직접 기록 요청.
5.  **Completion**: 전송 완료 시 ASIO 핸들러가 트리거되어 다음 전송 수행.

### 메시지 수신 (Receive Path)
1.  **ASIO Engine**: 상시 `async_read` 대기 상태.
2.  **OS**: 데이터 도착 시 Engine의 핸들러 호출.
3.  **Engine**: ZMTP 프로토콜 파싱 후 `Session`으로 메시지 푸시.
4.  **Session**: 파이프를 통해 `Socket`으로 전달.
5.  **User**: `zmq_recv()`로 데이터 수신.

---

## 5. 지원 소켓 타입 (Supported Sockets)

ZLink는 통신 패턴의 복잡성을 줄이기 위해 다음 6가지 핵심 소켓만 지원합니다.

| 소켓 타입 | 패턴 | 설명 | 용도 |
| :--- | :--- | :--- | :--- |
| **ROUTER** | Async Server | 연결 식별자(Identity)를 통해 특정 클라이언트와 통신 | 서버, 브로커 |
| **DEALER** | Async Client | 비동기 요청/응답, 로드 밸런싱 지원 | 클라이언트, 워커 |
| **PUB** | Publish | 데이터 브로드캐스팅 (단방향) | 데이터 배포 |
| **SUB** | Subscribe | 데이터 구독 (필터링 가능) | 데이터 수신 |
| **PAIR** | Exclusive | 1:1 독점 연결 (스레드 간 통신 등) | 제어 채널 |
| **STREAM** | Raw TCP | ZMQ 헤더 없이 순수 TCP/Byte 스트림 처리 | 게이트웨이, 레거시 연동 |

---

## 6. 기존 LibZMQ vs ZLink 비교

| 기능 | Legacy LibZMQ | **ZLink (New)** |
| :--- | :--- | :--- |
| **I/O 모델** | Reactor (poll, epoll, kqueue, select) | **Proactor (Boost.Asio)** |
| **API** | 동기식 `poll()` 위주 | **비동기 Callback/Handler 위주** |
| **암호화** | Curve, Sodium 내장 | **제거됨 (외부 처리 권장)** |
| **Socket** | REQ, REP 등 10+ 종 | **핵심 6종 (REQ/REP 제거됨)** |
| **Build** | Autotools/CMake 복합 | **CMake 중심, 간소화된 스크립트** |

---

## 7. 주요 파일 및 디렉토리 구조

```text
doc/arch/
└── ARCHITECTURE.md       <-- 본 문서

src/
├── asio/                 <-- [핵심] ASIO 구현체
│   ├── asio_poller.hpp   # io_context 래퍼
│   ├── asio_engine.hpp   # Proactor 엔진
│   ├── asio_zmtp_engine.hpp # ZMTP 프로토콜 구현
│   └── *_transport.hpp   # TCP/WS 전송 계층
├── thread.cpp            # 스레드 관리
├── io_thread.cpp         # I/O 스레드와 Poller 연결
├── socket_base.cpp       # 소켓 공통 로직
├── session_base.cpp      # 세션 관리
└── [socket_type].cpp     # 각 소켓 구현체 (router, dealer 등)
```
