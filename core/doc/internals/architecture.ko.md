[English](architecture.md) | [한국어](architecture.ko.md)

# zlink 시스템 아키텍처 - 내부 개발자 참조 문서

이 문서는 **zlink** 라이브러리의 내부 아키텍처를 상세히 기술합니다.
대상 독자는 zlink 라이브러리 자체를 개발하거나 유지보수하는 **내부 개발자**이며,
시스템의 계층 구조, 핵심 컴포넌트, 데이터 흐름, 소스 트리를 포괄적으로 다룹니다.

---

## 목차

1. [개요 및 설계 철학](#1-개요-및-설계-철학)
2. [I/O 모델: Proactor 패턴](#2-io-모델-proactor-패턴)
3. [5계층 아키텍처](#3-5계층-아키텍처)
4. [컴포넌트 연결 관계](#4-컴포넌트-연결-관계)
5. [Socket Logic Layer 상세](#5-socket-logic-layer-상세)
6. [Engine Layer 상세](#6-engine-layer-상세)
7. [핵심 컴포넌트](#7-핵심-컴포넌트)
8. [데이터 흐름](#8-데이터-흐름)
9. [소스 트리 구조](#9-소스-트리-구조)
10. [구조 설계 철학](#10-구조-설계-철학)

---

## 1. 개요 및 설계 철학

### 1.1 zlink란?

zlink 는 libzmq 의 소켓 패턴과 API 형태를 공유하는 고성능 메시징
라이브러리다. 다음 설계 요소가 적용되어 있다.

- **Boost.Asio 기반 I/O**: 플랫폼별 폴러(epoll/kqueue/IOCP) 대신 Asio의 통합 비동기 I/O 사용
- **WebSocket/TLS 네이티브 지원**: `ws://`, `wss://`, `tls://` 프로토콜을 라이브러리 수준에서 내장
- **자체 프로토콜 스택**: ZMTP 대신 경량화된 **ZMP v1.0** 프로토콜 사용

### 1.2 설계 원칙

| 원칙 | 설명 |
|------|------|
| Zero-Copy | 메시지 복사 최소화를 통한 메모리 대역폭 절약 |
| Lock-Free | 스레드 간 통신에 락-프리(lock-free) 자료구조(YPipe — 단방향 비잠금 큐) 사용 |
| True Async | Proactor 패턴(완료 기반 비동기 I/O 모델) 기반의 진정한 비동기 I/O |
| Protocol Agnostic | 트랜스포트와 프로토콜의 명확한 분리 |

### 1.3 지원 소켓 및 트랜스포트

**소켓 패턴 (7종)**

| 소켓        | 유형             | 설명                              |
|-------------|-----------------|-----------------------------------|
| PAIR        | 1:1 양방향       | 단일 연결, 양방향 통신            |
| PUB / SUB   | 발행-구독        | 토픽 기반 브로드캐스트            |
| XPUB / XSUB | 확장 발행-구독    | 구독 메시지 접근 가능             |
| DEALER      | 비동기 요청      | 라운드로빈 분배                   |
| ROUTER      | ID 기반 라우팅   | 다중 클라이언트 라우팅            |
| STREAM      | RAW TCP          | 외부 클라이언트 연동              |

**트랜스포트 (6종)**

| 스킴       | 설명                                      |
|------------|-------------------------------------------|
| `tcp://`   | 표준 TCP                                  |
| `ipc://`   | Unix 도메인 소켓 (Unix/Linux/macOS)       |
| `inproc://`| 프로세스 내 통신 (락-프리 파이프, 네트워크 스택 우회)       |
| `ws://`    | WebSocket (Beast 라이브러리)              |
| `wss://`   | WebSocket over TLS                        |
| `tls://`   | 네이티브 TLS (OpenSSL)                    |

---

## 2. I/O 모델: Proactor 패턴

zlink 의 I/O 코어는 Boost.Asio 기반의 **Proactor 패턴** 을 사용한다.
Proactor 패턴은 엔진이 OS 에 비동기 I/O 연산을 요청하면 OS 가 완료 후 콜백(완료 통지 함수)을 호출하는
"완료 기반(completion-based)" 모델이다.
비교를 위해 libzmq 의 전통적인 **Reactor 패턴** 을 함께 살펴본다.
Reactor 패턴은 fd(파일 디스크립터) 준비 상태를 감시하다가 엔진이 직접 read/write 를
수행하는 "준비 기반(readiness-based)" 모델이다.

### 2.1 Reactor 패턴 (libzmq, 비교용)

libzmq는 전형적인 **Reactor 패턴**을 사용합니다.
중앙의 폴러(`poller_t`)가 fd 의 준비 상태(읽기/쓰기 가능 여부)를 감시하고
준비된 fd 에 대해 엔진의 핸들러를 호출하는 구조입니다.

```
+------------------------------------------------------------------+
|                    libzmq Reactor Model                          |
+------------------------------------------------------------------+
|                                                                  |
|   +---------------------------------------------------------+    |
|   |              poller_t (Central Event Loop)                |  |
|   |                                                              |
|   |   epoll_wait() / kqueue() / select() / IOCP                  |
|   |              |                                               |
|   |              v                                               |
|   |   +----------------------+                               |   |
|   |   |  fd ready (readable) |--→ engine->in_event()        |    |
|   |   |  fd ready (writable) |--→ engine->out_event()       |    |
|   |   |  fd error            |--→ engine->in_event()        |    |
|   |   +----------------------+                               |   |
|   |                                                              |
|   +---------------------------------------------------------+    |
|                                                                  |
|   Flow: register fd → wait for readiness → notify → read/write   |
|   Key: poller says "ready to read", then engine calls read()     |
|                                                                  |
+------------------------------------------------------------------+
```

**핵심 특성:**
- 플랫폼별 폴러 구현 필요 (epoll, kqueue, devpoll, pollset, select, IOCP)
- 엔진이 `in_event()`/`out_event()` 콜백에서 동기 `read()`/`write()` 수행
- 각 I/O 스레드가 `poller_t` 인스턴스를 하나씩 소유하고 이벤트 루프를 실행
- 새로운 트랜스포트 추가 시 fd 기반 인터페이스에 맞춰야 하는 제약

### 2.2 Proactor 패턴 (zlink)

zlink는 Boost.Asio의 **Proactor 패턴**을 사용합니다.
엔진이 OS 에 비동기 I/O 연산을 요청하면 OS 가 완료 후 콜백을 호출하는 구조입니다.

```
+------------------------------------------------------------------+
|                    zlink Proactor Model                          |
+------------------------------------------------------------------+
|                                                                  |
|   +---------------------------------------------------------+    |
|   |              asio_engine_t (Async Engine)                 |  |
|   |                                                              |
|   |   (1) async_read_some(buffer, handler)                       |
|   |       |  Delegate read to OS                                 |
|   |       +--→ [OS kernel performs I/O] --→ on_read_complete()   |
|   |                                                              |
|   |   (2) async_write_some(buffer, handler)                      |
|   |       |  Delegate write to OS                                |
|   |       +--→ [OS kernel performs I/O] --→ on_write_complete()  |
|   |                                                              |
|   +---------------------------------------------------------+    |
|                                                                  |
|   +---------------------------------------------------------+    |
|   |              io_context (Boost.Asio)                      |  |
|   |                                                              |
|   |   io_context::run()                                          |
|   |   - Dispatches completion handlers for finished ops          |
|   |   - One io_context per I/O thread, single-threaded           |
|   |                                                              |
|   +---------------------------------------------------------+    |
|                                                                  |
|   Flow: request async op → OS completes I/O → completion call    |
|   Key: engine never performs I/O directly, only handles results  |
|                                                                  |
+------------------------------------------------------------------+
```

**핵심 특성:**
- Boost.Asio가 플랫폼별 차이를 추상화 (epoll/kqueue/IOCP를 통합)
- 엔진이 `async_read_some()`/`async_write_some()`으로 연산을 요청하고 완료 콜백에서 결과를 처리
- 각 I/O 스레드가 독립된 `io_context` 를 소유 — 스레드 간 경합 없음
- 트랜스포트 추상화(`i_asio_transport`)를 통해 TCP/TLS/WS/WSS를 동일한 인터페이스로 처리

### 2.3 Reactor vs. Proactor 비교

| 항목 | libzmq (Reactor) | zlink (Proactor) |
|------|------------------|------------------|
| I/O 모델 | Readiness 기반<br/>"읽을 수 있다" → read() | Completion 기반<br/>"읽기 완료됨" → 콜백 호출 |
| 메인 루프 | poller_t::loop()<br/>(자체 이벤트 루프) | io_context::run()<br/>(Boost.Asio 이벤트 루프) |
| I/O 스레드 | 스레드당 poller_t<br/>+ fd_table 관리 | 스레드당 io_context<br/>+ 독립 실행 |
| 엔진 콜백 | in_event() / out_event() | on_read_complete()<br/>on_write_complete() |
| 프로토콜 | ZMTP 3.x | ZMP v1.0 (8B 고정 헤더) |
| 트랜스포트 추가 비용 | fd 직접 관리<br/>폴러에 fd 등록 필요 | i_asio_transport 추상화<br/>인터페이스 구현만으로 확장 |
| 플랫폼 폴러 | 6종 직접 구현<br/>(epoll,kqueue,IOCP 등) | Boost.Asio에 위임<br/>(단일 코드베이스) |
| 최적화 | Reactor 이벤트 배칭 | Speculative I/O<br/>Gather Write<br/>Backpressure (pending buf) |

### 2.4 계층 구성

zlink 는 socket-logic 레벨의 building block 은 libzmq 와 공유하고
I/O 코어는 Asio 기반 Proactor 로 구성하며 자체 기능을 별도로 쌓는다.
아래 다이어그램은 각 계층이 현재 코드베이스에서 어떻게 구성되는지 보여 준다.

```
+---------------------------------------------------------------------+
|                   Per-Layer: Shared / Asio / Added                  |
+---------------------------------------------------------------------+
|                                                                     |
|  ■ Shared with libzmq (socket-logic building block)                 |
|  +---------------------------------------------------------------+  |
|  |  Socket Logic Layer                                           |  |
|  |  - socket_base_t, pair_t, dealer_t, router_t, pub_t, sub_t   |   |
|  |  - Routing strategies: lb_t, fq_t, dist_t                    |   |
|  |  - Subscription management: mtrie_t, radix_tree_t             |  |
|  +---------------------------------------------------------------+  |
|  |  Inter-Thread Infrastructure                                  |  |
|  |  - YPipe (Lock-free queue, CAS-based)                         |  |
|  |  - pipe_t (Bidirectional message pipe)                        |  |
|  |  - mailbox_t + signaler_t (Inter-thread command delivery)     |  |
|  |  - command_t (20 internal command types)                      |  |
|  +---------------------------------------------------------------+  |
|  |  Message System                                               |  |
|  |  - msg_t (64-byte fixed, VSM/LMSG/CMSG/ZCLMSG)              |    |
|  +---------------------------------------------------------------+  |
|                                                                     |
|  ■ Asio 기반 I/O 코어                                               |
|  +---------------------------------------------------------------+  |
|  |  asio_poller_t                                                |  |
|  |  - mailbox fd 모니터링용 최소 reactor 래퍼                    |  |
|  +---------------------------------------------------------------+  |
|  |  asio_engine_t (Proactor)                                     |  |
|  |  - 완료 기반 I/O 엔진                                          |  |
|  +---------------------------------------------------------------+  |
|  |  i_asio_transport 인터페이스                                   |  |
|  |  - TCP/IPC 를 Boost.Asio 소켓으로 래핑                        |  |
|  +---------------------------------------------------------------+  |
|  |  ZMP v1.0                                                     |  |
|  |  - 8-byte 고정 헤더, HELLO/READY 핸드셰이크                   |  |
|  +---------------------------------------------------------------+  |
|                                                                     |
|  ■ zlink 가 추가한 구성 요소                                         |
|  +---------------------------------------------------------------+  |
|  |  Speculative I/O                                              |  |
|  |  - async 전에 sync 시도 → fast path 콜백 오버헤드 제거         |  |
|  +---------------------------------------------------------------+  |
|  |  Backpressure (pending_buffers)                               |  |
|  |  - HWM 도달 시 수신 데이터를 10MB 까지 버퍼링                  |  |
|  +---------------------------------------------------------------+  |
|  |  Gather Write                                                 |  |
|  |  - scatter/gather I/O 로 헤더+payload 를 단일 syscall 로 전송 |  |
|  +---------------------------------------------------------------+  |
|  |  Native WS/WSS/TLS Transports                                 |  |
|  |  - Beast WebSocket + OpenSSL 을 i_asio_transport 로 통합      |  |
|  +---------------------------------------------------------------+  |
|  |  Service Layer (SPOT)                                         |  |
|  |  - socket 위에 올리는 상위 서비스 추상                         |  |
|  +---------------------------------------------------------------+  |
|                                                                     |
+---------------------------------------------------------------------+
```

**왜 최소 reactor 래퍼를 남겼는가?**

`asio_poller_t` 는 mailbox fd 를 감시하기 위한 최소한의 reactor 호환 래퍼다.
`io_object_t` 인프라가 mailbox 이벤트를 poller 콜백으로 수신하므로 이 경로는
Asio 의 `async_wait()` 로 래핑되어 reactor 모양의 경로 위에 그대로 올라간다.
실제 데이터 I/O 경로(`asio_engine_t`) 는 순수 Proactor 패턴으로 동작한다.

---

## 3. 5계층 아키텍처

zlink는 5개의 명확히 분리된 계층으로 구성됩니다.
각 계층은 단일 책임을 가지며, 아래로 갈수록 물리적 네트워크에 가까워집니다.

```
+-------------------------------------------------------------------------+
|                          APPLICATION LAYER                              |
|                                                                         |
|   User code:                                                            |
|   zlink_ctx_new() -> zlink_socket() -> zlink_bind/connect()             |
|   -> zlink_send() / zlink_recv() -> zlink_close()                       |
|                                                                         |
+-------------------------------------------------------------------------+
|                           PUBLIC API LAYER                              |
|                                                                         |
|   src/api/core/zlink.cpp                                                     |
|   - C API entry points (zlink_socket, zlink_send, zlink_recv, etc.)     |
|   - Error handling and parameter validation                             |
|                                                                         |
+-------------------------------------------------------------------------+
|                          SOCKET LOGIC LAYER                             |
|                                                                         |
|   src/runtime/sockets/                                                          |
|   - socket_base_t: Base class for all sockets                           |
|   - pair_t, dealer_t, router_t, pub_t, sub_t, xpub_t, xsub_t, stream_t  |
|   - Routing strategies: lb_t(RR), fq_t(Fair Queue), dist_t(Fan-out)     |
|   - Subscription management: mtrie_t(XPUB), radix_tree_t /              |
|     trie_with_size_t(XSUB)                                              |
|                                                                         |
+-------------------------------------------------------------------------+
|                          ENGINE LAYER (ASIO)                            |
|                                                                         |
|   src/runtime/engine/asio/                                                      |
|   - asio_engine_t      : Proactor pattern-based async I/O engine (base) |
|   - asio_zmp_engine_t  : ZMP protocol (8B fixed header + handshake)     |
|   - asio_raw_engine_t  : RAW protocol (no framing, STREAM only)         |
|                                                                         |
+-------------------------------------------------------------------------+
|                          PROTOCOL LAYER                                 |
|                                                                         |
|   +---------------------------+    +---------------------------+        |
|   |    ZMP v1.0 Protocol      |    |     RAW Protocol          |        |
|   |    src/runtime/protocol/zmp_*     |    |     src/runtime/protocol/raw_*    |        |
|   |    - 8-byte fixed header  |    |     - no framing          |        |
|   |    - Handshake support    |    |     - No handshake        |        |
|   +---------------------------+    +---------------------------+        |
|                                                                         |
+-------------------------------------------------------------------------+
|                          TRANSPORT LAYER                                |
|                                                                         |
|   src/runtime/transports/                                                       |
|   +---------+  +---------+  +---------+  +----------+                   |
|   |   TCP   |  |   IPC   |  |   WS    |  | TLS/WSS  |                   |
|   |  tcp_   |  |  ipc_   |  |  ws_    |  |  ssl_    |                   |
|   |transport|  |transport|  |transport|  |transport |                   |
|   +---------+  +---------+  +---------+  +----------+                   |
|                                                                         |
|   i_asio_transport: Unified async interface for all transports          |
|                                                                         |
+-------------------------------------------------------------------------+
```

**계층 간 메시지 전달 경로**:
- 하향 (송신): Application -> API -> Socket Logic -> pipe_t -> Engine -> Protocol -> Transport
- 상향 (수신): Transport -> Protocol -> Engine -> pipe_t -> Socket Logic -> API -> Application

---

## 4. 컴포넌트 연결 관계

아래 다이어그램은 zlink 내부 객체들의 소유 관계와 상호작용을 보여줍니다.

```
+----------------------------------------------------------------------+
|                              ctx_t                                   |
|  (Global context: I/O thread pool, socket management, inproc         |
|   endpoints)                                                         |
+--------------------------------+-------------------------------------+
                                 | owns
            +--------------------+--------------------+
            |                    |                    |
            v                    v                    v
    +---------------+   +---------------+   +---------------+
    |  socket_base_t|   |  io_thread_t  |   |   reaper_t    |
    | (socket inst.) |   | (I/O worker)  |   |(resource      |
    |               |   |               |   | cleanup)      |
    +-------+-------+   +-------+-------+   +---------------+
            |                   |
            | owns              | runs
            v                   v
    +---------------+   +---------------+
    | session_base_t|   |  io_context   |
    | (session mgmt)|   | (Asio reactor)|
    +-------+-------+   +---------------+
            |
     +------+------+
     |             |
     v             v
+---------+  +-------------                                            +
| pipe_t  |  |asio_engine_t                                            |
|(msg que)|  | (I/O engine)                                            |
+---------+  +------+------                                            +
                    |
                    v
            +---------------+
            |i_asio_transport|
            |  (transport)  |
            +---------------+
```

**주요 소유 관계 설명**:

- `ctx_t`는 모든 `socket_base_t`, `io_thread_t`, `reaper_t`를 소유합니다.
- `socket_base_t`는 `session_base_t`를 소유하며, 세션(session)은 소켓과 엔진 사이의 브리지 역할을 합니다.
- `session_base_t`는 `pipe_t`(락-프리 메시지 큐)와 `asio_engine_t`(I/O 엔진)를 소유합니다.
- `asio_engine_t`는 `i_asio_transport` 인터페이스를 통해 물리적 전송 계층과 통신합니다.
- `io_thread_t`는 독립적인 `io_context` 를 보유하여 비동기 I/O 를 처리합니다.
- `reaper_t`는 종료된 소켓/세션의 자원을 안전하게 정리합니다.

---

## 5. Socket Logic Layer 상세

### 5.1 클래스 계층 구조

```
socket_base_t (base class)
+-- pair_t              # PAIR socket: 1:1 bidirectional communication
+-- dealer_t            # DEALER socket: async request, round-robin
+-- router_t            # ROUTER socket: ID-based routing (inherits routing_socket_base_t)
+-- xpub_t              # XPUB socket: can receive subscription messages
|   +-- pub_t           # PUB socket: simplified XPUB (no subscription exposure)
+-- xsub_t              # XSUB socket: receives all without local filter (proxy use)
|   +-- sub_t           # SUB socket: simplified XSUB (subscribe via setsockopt)
+-- stream_t            # STREAM socket: RAW TCP, external client integration
```

`socket_base_t`는 모든 소켓의 공통 기능을 제공합니다:
- 연결 관리 (`bind`, `connect`, `disconnect`, `unbind`)
- 파이프 관리 (생성, 종료, 활성화)
- 옵션 관리 (`setsockopt`, `getsockopt`)
- 폴링 지원 (`has_in`, `has_out`)

### 5.2 라우팅 전략 클래스

소켓 타입별로 메시지 분배와 수집에 사용되는 전략 클래스가 분리되어 있습니다:

```
+----------------------------------------------------------------------+
|                     Routing Strategies                               |
+----------------------------------------------------------------------+
|                                                                      |
|  +-------------------------------------------------------------+     |
|  |  lb_t (Load Balancer) - Sender-side round-robin                   |
|  |                                                               |   |
|  |  Pipe A --→ [ msg1 ]                                              |
|  |  Pipe B --→ [ msg2 ]    ← Distributes in order                    |
|  |  Pipe C --→ [ msg3 ]                                              |
|  |                                                               |   |
|  |  Used by: DEALER (Tx)                                             |
|  +-------------------------------------------------------------+     |
|                                                                      |
|  +-------------------------------------------------------------+     |
|  |  fq_t (Fair Queue) - Receiver-side fair queue                     |
|  |                                                               |   |
|  |  Pipe A ←-- [ msg ]                                               |
|  |  Pipe B ←-- [ msg ]    ← Fairly receives from each pipe     |     |
|  |  Pipe C ←-- [ msg ]                                               |
|  |                                                               |   |
|  |  Used by: DEALER (Rx), SUB (Rx)                                   |
|  +-------------------------------------------------------------+     |
|                                                                      |
|  +-------------------------------------------------------------+     |
|  |  dist_t (Distributor) - Broadcast fan-out                         |
|  |                                                               |   |
|  |  [ msg ] --→ Pipe A                                               |
|  |          --→ Pipe B    ← Sends the same message to all pipes      |
|  |          --→ Pipe C                                               |
|  |                                                               |   |
|  |  Used by: PUB, XPUB (Tx)                                    |     |
|  +-------------------------------------------------------------+     |
|                                                                      |
+----------------------------------------------------------------------+
```

### 5.3 소켓별 라우팅 전략 매핑

| 소켓    | 송신 (Tx)             | 수신 (Rx)            | 비고                         |
|---------|-----------------------|----------------------|------------------------------|
| PAIR    | 단일 파이프            | 단일 파이프           | 파이프 1개만 허용            |
| DEALER  | `lb_t` (Round-robin)  | `fq_t` (Fair Queue)  | 비동기 요청-응답             |
| ROUTER  | ID 기반 직접 라우팅    | `fq_t` (Fair Queue)  | Routing ID로 대상 파이프 검색|
| PUB     | `dist_t` (Fan-out)    | -                    | 수신 불가                    |
| SUB     | -                     | `fq_t` (Fair Queue)  | 토픽 필터링 적용             |
| XPUB    | `dist_t` (Fan-out)    | 구독 메시지 수신      | mtrie_t로 구독 관리          |
| XSUB    | -                     | `fq_t` (Fair Queue)  | 로컬 필터 없이 전체 수신     |
| STREAM  | ID 기반 직접 라우팅    | `fq_t` (Fair Queue)  | RAW 프로토콜 사용            |

### 5.4 구독 자료구조

PUB/SUB 패턴에서 토픽 매칭에 사용되는 트라이 기반 자료구조:

```
+-------------------------------------------------------------+
|                 Subscription Topic Trie Structure           |
|                                                             |
|                       (root)                                |
|                      /      \                               |
|                  "news"    "stock"                          |
|                   /          /   \                          |
|              "weather"   "AAPL"  "GOOGL"                    |
|                                                             |
|  - XPUB: mtrie_t (multi-trie, per-pipe subscription tracking|
|  - XSUB: Depends on ZLINK_USE_RADIX_TREE macro              |
|    - radix_tree_t (when enabled, memory-efficient)          |
|    - trie_with_size_t (default, fast lookup)                |
|  - Lookup complexity: O(m), m = topic string length         |
|                                                             |
+-------------------------------------------------------------+
```

---

## 6. Engine Layer 상세

### 6.1 엔진 타입 비교

Engine Layer는 Boost.Asio 기반의 비동기 I/O 처리를 담당합니다.

| 엔진                  | 프로토콜  | 트랜스포트              | 특징                            |
|-----------------------|-----------|------------------------|---------------------------------|
| `asio_zmp_engine_t`   | ZMP v1.0  | TCP, TLS, IPC, WS, WSS | 핸드셰이크 + 8바이트 고정 헤더  |
| `asio_raw_engine_t`   | RAW       | TCP, TLS, IPC, WS, WSS | 프레이밍 없음, STREAM 전용 |

> WS/WSS도 `asio_zmp_engine_t` 또는 `asio_raw_engine_t`를 사용하며,
> WebSocket 프레이밍은 `ws_transport_t`/`wss_transport_t`가 처리합니다.

### 6.2 Proactor 패턴 구조

```
+----------------------------------------------------------------------+
|                     Routing Strategies                               |
+----------------------------------------------------------------------+
|                                                                      |
|  +-------------------------------------------------------------+     |
|  |  lb_t (Load Balancer) - Sender-side round-robin                   |
|  |                                                               |   |
|  |  Pipe A --→ [ msg1 ]                                              |
|  |  Pipe B --→ [ msg2 ]    ← Distributes in order                    |
|  |  Pipe C --→ [ msg3 ]                                              |
|  |                                                               |   |
|  |  Used by: DEALER (Tx)                                             |
|  +-------------------------------------------------------------+     |
|                                                                      |
|  +-------------------------------------------------------------+     |
|  |  fq_t (Fair Queue) - Receiver-side fair queue                     |
|  |                                                               |   |
|  |  Pipe A ←-- [ msg ]                                               |
|  |  Pipe B ←-- [ msg ]    ← Fairly receives from each pipe     |     |
|  |  Pipe C ←-- [ msg ]                                               |
|  |                                                               |   |
|  |  Used by: DEALER (Rx), SUB (Rx)                                   |
|  +-------------------------------------------------------------+     |
|                                                                      |
|  +-------------------------------------------------------------+     |
|  |  dist_t (Distributor) - Broadcast fan-out                         |
|  |                                                               |   |
|  |  [ msg ] --→ Pipe A                                               |
|  |          --→ Pipe B    ← Sends the same message to all pipes      |
|  |          --→ Pipe C                                               |
|  |                                                               |   |
|  |  Used by: PUB, XPUB (Tx)                                    |     |
|  +-------------------------------------------------------------+     |
|                                                                      |
+----------------------------------------------------------------------+
```

### 6.3 엔진 상태 머신

```
          +---------------------+
          |      Created        |
          +----------+----------+
                     | plug()
                     v
          +---------------------+
          |    Handshaking      |  TLS/WebSocket: transport handshake
          |  (_handshaking)     |  ZMP: protocol handshake
          +----------+----------+
                     | handshake complete
                     v
          +---------------------+
          |      Active         | <-----------------+
          |   Data send/recv    |                   |
          +----------+----------+                   |
                     | I/O error| restart
                     v                              |
          +---------------------+                   |
          |       Error         | -----------------+
          +----------+----------+
                     | terminate()
                     v
          +---------------------+
          |    Terminated       |
          +---------------------+
```

### 6.4 ZMP v1.0 프레임 구조

```
 Byte:   0         1         2         3         4    5    6    7
      +---------+---------+---------+---------+---------------------+
      |  MAGIC  | VERSION |  FLAGS  |RESERVED |   PAYLOAD SIZE      |
      |  (0x5A) |  (0x01) |         | (0x00)  |   (32-bit BE)       |
      +---------+---------+---------+---------+---------------------+
```

| 필드         | 오프셋 | 크기 | 설명                    |
|-------------|--------|------|-------------------------|
| MAGIC       | 0      | 1    | 매직 넘버 `0x5A` ('Z')  |
| VERSION     | 1      | 1    | 프로토콜 버전 `0x01`    |
| FLAGS       | 2      | 1    | 프레임 플래그            |
| RESERVED    | 3      | 1    | 예약 (0x00)              |
| PAYLOAD SIZE| 4-7    | 4    | payload 크기 (Big Endian)|

**FLAGS 비트 정의**:

| 비트 | 이름      | 설명               |
|------|-----------|--------------------|
| 0    | MORE      | 멀티파트 메시지 계속|
| 1    | CONTROL   | 제어 프레임         |
| 2    | IDENTITY  | 라우팅 ID 포함      |
| 3    | SUBSCRIBE | 구독 요청           |
| 4    | CANCEL    | 구독 취소           |

### 6.5 RAW 프로토콜 프레임 구조

STREAM 소켓과 외부 클라이언트 연동용 프로토콜이다. 별도 프레이밍 헤더를 붙이지
않고 메시지 바이트를 그대로 주고받으며, 메시지 경계는 애플리케이션이 정의한다.

```
+-------------------------------------------------+
|              Payload (N Bytes, as-is)           |
+-------------------------------------------------+
```

- 핸드셰이크 없음 (즉시 데이터 송수신)
- `raw_encoder_t` 는 메시지 바이트를 그대로 내보내고, `raw_decoder_t` 는 수신한
  바이트 span 을 그대로 메시지로 만든다 (추가 프레이밍 없음)
- 외부 클라이언트 연동 용이
- `zlink_stream_packet_handler()` 로 packet handler 모드를 켜면
  `header_size(2B) + body_size(4B)` 형태의 length-prefixed packet 프레이밍을
  파싱한다 (자세한 내용은 [RAW 프로토콜 상세](protocol-raw.ko.md))

### 6.6 ZMP 핸드셰이크 시퀀스

```
    Client                              Server
       |                                   |
       |------- HELLO (greeting) ---------→|
       |                                   |
       |←------ HELLO (greeting) ----------|
       |                                   |
       |                                   |  (Socket type compatibility check)
       |                                   |
       |------- READY (metadata) ---------→|
       |                                   |
       |←------ READY (metadata) ----------|
       |                                   |
       |←------- Data Exchange -----------→|
       |                                   |
```

- **HELLO**: control 프레임 타입(1B) + 소켓 타입(1B) + Identity 길이(1B) + Identity 값(0-255B)
- **READY**: `zmp_metadata` 옵션이 켜진 경우 `Socket-Type` 속성을 싣고, DEALER/ROUTER 는 `Routing-Id` 속성을 추가한다

### 6.7 프로토콜-트랜스포트-엔진 매핑

소켓 타입에 따라 엔진이 자동 선택됩니다:

```
+---------------------------------------------------------------------+
|                       Engine Selection Rules                        |
+---------------------------------------------------------------------+
|                                                                     |
|  Socket type == STREAM ?                                            |
|      +- YES → asio_raw_engine_t  (RAW protocol, no handshake)       |
|      +- NO  → asio_zmp_engine_t  (ZMP protocol, HELLO/READY)        |
|                                                                     |
|  This rule is the same across all transports (TCP/TLS/IPC/WS/WSS).  |
|                                                                     |
+---------------------------------------------------------------------+
```

**전체 매핑 매트릭스**:

| URL 스킴  | Connecter               | Transport          | STREAM 엔진         | 기타 소켓 엔진       | 핸드셰이크        |
|-----------|-------------------------|--------------------|---------------------|---------------------|-------------------|
| `tcp://`  | `asio_tcp_connecter_t`  | `tcp_transport_t`  | `asio_raw_engine_t` | `asio_zmp_engine_t` | (없음) / ZMP      |
| `tls://`  | `asio_tls_connecter_t`  | `ssl_transport_t`  | `asio_raw_engine_t` | `asio_zmp_engine_t` | SSL / SSL+ZMP     |
| `ws://`   | `asio_ws_connecter_t`   | `ws_transport_t`   | `asio_raw_engine_t` | `asio_zmp_engine_t` | WS / WS+ZMP      |
| `wss://`  | `asio_ws_connecter_t`   | `wss_transport_t`  | `asio_raw_engine_t` | `asio_zmp_engine_t` | SSL+WS / SSL+WS+ZMP|
| `ipc://`  | `asio_ipc_connecter_t`  | `ipc_transport_t`  | `asio_raw_engine_t` | `asio_zmp_engine_t` | (없음) / ZMP      |

### 6.8 핸드셰이크 단계 비교

```
+-------------------------------------------------------------------------+
|                      Handshake Stage Comparison                         |
+-------------------------------------------------------------------------+
|                                                                         |
|  TCP + PAIR/DEALER/ROUTER/PUB/SUB                                       |
|  +---------+    +-------------+                                         |
|  |  TCP    |---→|  ZMP        |---→ Data Transfer                       |
|  | Connect |    |  Handshake  |                                         |
|  +---------+    +-------------+                                         |
|                                                                         |
|  TCP + STREAM                                                           |
|  +---------+                                                            |
|  |  TCP    |-----------------------→ Data Transfer (immediate)          |
|  | Connect |                                                            |
|  +---------+                                                            |
|                                                                         |
|  TLS + PAIR/DEALER/ROUTER/PUB/SUB                                       |
|  +---------+    +---------+    +-------------+                          |
|  |  TCP    |---→|  SSL    |---→|  ZMP        |---→ Data Transfer        |
|  | Connect |    |Handshake|    |  Handshake  |                          |
|  +---------+    +---------+    +-------------+                          |
|                                                                         |
|  WS + PAIR/DEALER/ROUTER/PUB/SUB                                        |
|  +---------+    +---------+    +-------------+                          |
|  |  TCP    |---→|   WS    |---→|  ZMP        |---→ Data Transfer        |
|  | Connect |    | Upgrade |    |  Handshake  |                          |
|  +---------+    +---------+    +-------------+                          |
|                                                                         |
|  WSS + PAIR/DEALER/ROUTER/PUB/SUB                                       |
|  +---------+    +---------+    +---------+    +-------------+           |
|  |  TCP    |---→|  SSL    |---→|   WS    |---→|  ZMP        |---→ Tx    |
|  | Connect |    |Handshake|    | Upgrade |    |  Handshake  |           |
|  +---------+    +---------+    +---------+    +-------------+           |
|                                                                         |
|  WSS + STREAM                                                           |
|  +---------+    +---------+    +---------+                              |
|  |  TCP    |---→|  SSL    |---→|   WS    |---------------→ Data Tx      |
|  | Connect |    |Handshake|    | Upgrade |                              |
|  +---------+    +---------+    +---------+                              |
|                                                                         |
+-------------------------------------------------------------------------+
```

### 6.9 트랜스포트 특성 비교

| 트랜스포트 | 핸드셰이크 | 암호화 | Speculative Write | Gather Write | 용도                    |
|-----------|:----------:|:------:|:-----------------:|:------------:|------------------------|
| TCP       | -          | -      | O                 | O            | 표준 네트워크 통신      |
| IPC       | -          | -      | 옵션              | O            | 로컬 프로세스 간 통신   |
| TLS       | O          | O      | -                 | -            | 암호화된 네트워크 통신  |
| WS        | O          | -      | -                 | -            | 웹 클라이언트 연동      |
| WSS       | O          | O      | -                 | -            | 암호화된 웹 클라이언트  |

---

## 7. 핵심 컴포넌트

### 7.1 msg_t - 메시지 컨테이너

모든 메시지 데이터를 담는 64바이트 고정 크기 구조체다.
`malloc` 호출 없이 작은 메시지를 처리하도록 설계되었다.
41바이트 이하는 VSM(Very Small Message, 구조체 내부 인라인 저장) 방식으로,
그 이상은 별도 할당 버퍼를 가리키는 포인터(LMSG)로 처리한다.

```
+-----------------------------------------------------------------+
|                        msg_t (64 bytes)                         |
+-----------------------------------------------------------------+
|                                                                 |
|  +-----------------------------------------------------------+  |
|  |  Common fields (base_t)                                      |
|  |  - metadata_t* metadata   (8 bytes)                          |
|  |  - uint32_t routing_id    (4 bytes)                          |
|  |  - group_t group          (16 bytes)                         |
|  |  - uint8_t flags          (1 byte)                           |
|  |  - uint8_t type           (1 byte)                           |
|  +-----------------------------------------------------------+  |
|                                                                 |
|  Type-specific data area (union):                               |
|                                                                 |
|  +-----------------------------------------------------------+  |
|  |  type_vsm (<=41B on 64-bit)                                  |
|  |  Very Small Message: data stored directly in msg_t buffer    |
|  |  - uint8_t data[max_vsm_size]                                |
|  |  - uint8_t size                                              |
|  |  -> Inline storage without malloc, fastest path              |
|  +-----------------------------------------------------------+  |
|                            OR                                   |
|  +-----------------------------------------------------------+  |
|  |  type_lmsg (>41B on 64-bit)                                  |
|  |  Large Message: pointer to separately allocated buffer       |
|  |  - content_t* content                                        |
|  |    +-- void* data          (data pointer)                    |
|  |    +-- size_t size         (size)                           ||
|  |    +-- msg_free_fn* ffn    (free function)                   |
|  |    +-- atomic_counter_t refcnt (reference count)             |
|  +-----------------------------------------------------------+  |
|                            OR                                   |
|  +-----------------------------------------------------------+  |
|  |  type_cmsg: Constant Message (const data ref, no free)       |
|  |  type_zclmsg: Zero-copy Large Message (direct user buffer)   |
|  +-----------------------------------------------------------+  |
|                                                                 |
+-----------------------------------------------------------------+
```

**메시지 플래그**:

| 플래그        | 값   | 설명                                |
|--------------|------|-------------------------------------|
| `more`       | 0x01 | 멀티파트 메시지의 중간 프레임        |
| `command`    | 0x02 | 제어 프레임 (핸드셰이크, 하트비트)  |
| `routing_id` | 0x40 | 라우팅 ID 포함                      |
| `shared`     | 0x80 | 공유 버퍼 (참조 카운팅)             |

**메시지 유형**:

| 유형           | 값  | 설명                                           |
|---------------|-----|------------------------------------------------|
| `type_vsm`    | 101 | VSM (Very Small Message, ≤41B — msg_t 내부 버퍼에 인라인 저장, malloc 없음) |
| `type_lmsg`   | 102 | Large Message (malloc'd 버퍼)                  |
| `type_cmsg`   | 104 | Constant Message (상수 데이터 참조)            |
| `type_zclmsg` | 105 | Zero-copy Large Message (사용자 버퍼 직접 사용)|

### 7.2 pipe_t - 락-프리 메시지 큐

스레드 간 메시지 전달을 위한 양방향 파이프다.
Application 스레드와 I/O 스레드 사이에서 `msg_t` 를 락-프리로 교환한다.
내부적으로 방향별 YPipe 두 개를 묶어 양방향 통신을 구성한다.

```
+---------------------------------------------------------------+
|                          pipe_t                               |
+---------------------------------------------------------------+
|                                                               |
|  Thread A (Socket)              Thread B (I/O)                |
|       |                                                       |
|       |    +------------------+     |                         |
|       +---→|   _out_pipe      |----→|  (Tx: Socket -> I/O)    |
|       |    |   (YPipe<msg_t>) |     |                         |
|       |    +------------------+     |                         |
|       |                                                       |
|       |    +------------------+     |                         |
|       |←---|   _in_pipe       |←----+  (Rx: I/O -> Socket)    |
|       |    |   (YPipe<msg_t>) |     |                         |
|       |    +------------------+     |                         |
|                                                               |
|  High Water Mark (HWM): Message queue size limit              |
|  - _hwm: Outbound HWM (blocks send when queue exceeded)       |
|  - _lwm: Inbound Low Water Mark (half of HWM, resume point)   |
|                                                               |
+---------------------------------------------------------------+
```

**YPipe 특성**:
- 락-프리 FIFO 큐 — CAS(Compare-And-Swap, 원자적 비교-교환 연산) 기반
- 캐시 라인 최적화 (false sharing 방지)
- 메모리 배리어를 통한 스레드 간 가시성 보장

**파이프 상태 머신**:

```
                    +------------+
                    |   active   | <----------------+
                    +-----+------+                  |
                          | receive delimiter       | connect
                          v                         |
              +-----------------------+             |
              | delimiter_received    |             |
              +-----------+-----------+             |
                          | send term_ack           |
                          v                         |
              +-----------------------+             |
              |    term_ack_sent      |             |
              +-----------+-----------+             |
                          | receive term_ack        |
                          v                         |
                    +-----------+                   |
                    | terminated| ------------------+
                    +-----------+     (on reconnect)
```

### 7.3 ctx_t - 컨텍스트

전역 상태를 관리하는 최상위 객체입니다.

**주요 역할**:

1. **I/O 스레드 풀 관리**
   - `zlink_ctx_set(ctx, ZLINK_IO_THREADS, n)`으로 스레드 수 설정 (기본: 4)
   - 각 I/O 스레드는 독립적인 `io_context` 보유
   - 새 연결 시 부하가 가장 적은 I/O 스레드 선택 (어피니티 마스크 지원)

2. **소켓 관리**
   - 소켓 생성/삭제 추적
   - 최대 소켓 수 제한 (기본: 4095)
   - 빈 슬롯 재사용

3. **inproc 엔드포인트(endpoint) 관리**
   - `inproc://name` 형식의 주소를 엔드포인트에 매핑
   - 바인드 전 연결 요청을 pending_connections 에 보관

```
ctx_t internal structure:
+----------------------------------------------------------+
|  _sockets: array_t<socket_base_t>     Active socket list |
|  _empty_slots: vector<uint32_t>       Empty slot reuse   |
|  _io_threads: vector<io_thread_t*>    I/O thread pool    |
|  _slots: vector<i_mailbox*>           Inter-thread mailbo|
|  _endpoints: map<string, endpoint_t>  inproc registry    |
|  _pending_connections: multimap       Pending connections|
|                                                          |
|  _max_sockets: int     (default: 4095)                   |
|  _io_thread_count: int (default: 4)                      |
|  _max_msgsz: int       (max message size)                |
+----------------------------------------------------------+
```

### 7.4 session_base_t - 세션

소켓과 엔진 사이의 브리지 역할을 하는 컴포넌트입니다.

```
+-------------------------------------------------------------+
|                     session_base_t                          |
+-------------------------------------------------------------+
|                                                             |
|  +--------------+    +---------+    +-----------------+     |
|  | socket_base_t|←--→| pipe_t  |←--→| asio_engine_t   |     |
|  |              |    |         |    |                 |     |
|  |  zlink_send() |    | YPipe   |    | async_read/     |    |
|  |  zlink_recv() |    |         |    | async_write     |    |
|  +--------------+    +---------+    +-----------------+     |
|                                                             |
|  push_msg(): Engine -> Session -> Pipe -> Socket            |
|  pull_msg(): Socket -> Pipe -> Session -> Engine            |
|                                                             |
|  Additional roles:                                          |
|  - Connection state management                              |
|  - Reconnection logic (exponential backoff)                 |
|  - Connecter selection (based on URL scheme)                |
|                                                             |
+-------------------------------------------------------------+
```

### 7.5 스레딩 모델

```
+-----------------------------------------------------------------+
|                    zlink Threading Model                        |
+-----------------------------------------------------------------+
|                                                                 |
|  +---------------------------------------------------------+    |
|  |                 Application Threads                          |
|  |  - Call zlink_send() / zlink_recv()                     |    |
|  |  - Recommended: access each socket from a single thread |    |
|  |  - Multiple sockets can be used from multiple threads   |    |
|  +--------------------------+------------------------------+    |
|                          |                                      |
|                   Lock-free Pipes (YPipe)                       |
|                          |                                      |
|  +--------------------------v------------------------------+    |
|  |                    I/O Threads                               |
|  |  +----------+ +----------+ +----------+                |     |
|  |  | Thread 0 | | Thread 1 | | Thread N |  (configurable) |    |
|  |  |io_context| |io_context| |io_context|                |     |
|  |  +----------+ +----------+ +----------+                |     |
|          |          |                                           |
|          |  - Asynchronous I/O processing (Proactor pattern)    |
|          |  - Encoder/decoder execution                         |
|          |  - Network send/receive                              |
|  +---------------------------------------------------------+    |
|                                                                 |
|  +---------------------------------------------------------+    |
|  |                    Reaper Thread                             |
|  |  - Resource cleanup for terminated sockets/sessions     |    |
|  |  - Deferred deletion processing                              |
|  +---------------------------------------------------------+    |
|                                                                 |
+-----------------------------------------------------------------+
```

**스레드 간 통신 (Mailbox 시스템)**:

```
Application Thread              I/O Thread
      |                              |
      |  zlink_send()                 |
      |      |                       |
      |      v                       |
      |  [Push msg_t to YPipe]       |
      |      |                       |
      |  mailbox.send(activate_write)|
      |-----------------------------→|
      |                              |  (signal received)
      |                              |
      |                              v
      |                         [Pop msg_t from YPipe]
      |                              |
      |                         [Encode and transmit]
```

- 각 스레드는 자신만의 `mailbox_t`를 보유합니다.
- `mailbox_t`는 내부적으로 `ypipe_t<command_t>` 와 `signaler_t` 로 구성됩니다.
- 명령 유형: `stop`, `plug`, `attach`, `bind`, `activate_read`, `activate_write` 등

---

## 8. 데이터 흐름

### 8.1 메시지 송신 (Outbound / Tx)

```
+-------------------------------------------------------------------+
|                    APPLICATION THREAD                             |
+-------------------------------------------------------------------+
|                                                                   |
|  (1) zlink_send(socket, parts, part_count, flags)                 |
|       |                                                           |
|       v                                                           |
|  (2) socket_base_t::send()                                        |
|       |  - Create msg_t (VSM or LMSG)                             |
|       |  - Select routing strategy by socket type                 |
|       |    . DEALER: lb_t (Round-robin)                           |
|       |    . ROUTER: ID-based direct routing                      |
|       |    . PUB: dist_t (send to all subscribers)                |
|       v                                                           |
|  (3) pipe_t::write()                                              |
|       |  - Push message to YPipe (Lock-free)                      |
|       |  - HWM check (block or drop when exceeded)                |
|       v                                                           |
|  (4) mailbox signal to I/O thread                                 |
|                                                                   |
+-------------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------------+
|                      I/O THREAD                                   |
+-------------------------------------------------------------------+
|                                                                   |
|  (5) asio_engine_t: receive activate_write event                  |
|       |                                                           |
|       v                                                           |
|  (6) pull_msg_from_session()                                      |
|       |  - Read message from pipe                                 |
|       v                                                           |
|  (7) encoder: message -> byte stream                              |
|       |  - ZMP: 8-byte header + payload                           |
|       |  - RAW: payload bytes as-is                               |
|       v                                                           |
|  (8) speculative_write() attempt                                  |
|       |  - Success: synchronous write completes immediately       |
|       |  - Failure (EAGAIN): schedule async_write_some()          |
|       v                                                           |
|  (9) transport: network transmission                              |
|       - TCP: direct send                                          |
|       - TLS: encrypt with SSL then send                           |
|       - WS: Beast WebSocket framing then send                     |
|                                                                   |
+-------------------------------------------------------------------+
```

### 8.2 메시지 수신 (Inbound / Rx)

```
+-------------------------------------------------------------------+
|                      I/O THREAD                                   |
+-------------------------------------------------------------------+
|                                                                   |
|  (1) async_read_some() completion callback                        |
|       |  - Receive bytes from network                             |
|       v                                                           |
|  (2) on_read_complete()                                           |
|       |                                                           |
|       v                                                           |
|  (3) decoder: byte stream -> message                              |
|       |  - Parse header (ZMP 8B, RAW none)                        |
|       |  - Verify payload size                                    |
|       |  - Create msg_t                                           |
|       v                                                           |
|  (4) push_msg_to_session()                                        |
|       |                                                           |
|       v                                                           |
|  (5) session_base_t::push_msg()                                   |
|       |  - Message validation                                     |
|       |  - Forward to inbound pipe                                |
|       v                                                           |
|  (6) pipe_t::write() (inbound pipe)                               |
|       |  - Push message to YPipe                                  |
|       v                                                           |
|  (7) Signal read-ready to socket (activate_read)                  |
|                                                                   |
+-------------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------------+
|                    APPLICATION THREAD                             |
+-------------------------------------------------------------------+
|                                                                   |
|  (8) zlink_recv(socket, &source_rid, &parts, &part_count, flags)  |
|       |                                                           |
|       v                                                           |
|  (9) socket_base_t::recv()                                        |
|       |  - Receive strategy by socket type                        |
|       |    . DEALER/SUB: fq_t (Fair Queueing)                     |
|       |    . ROUTER: extract Routing ID then deliver message      |
|       |  - Topic filtering (SUB)                                  |
|       v                                                           |
|  (10) pipe_t::read()                                              |
|        |  - Pop message from YPipe (Lock-free)                    |
|        v                                                          |
|  (11) Copy data to user buffer                                    |
|                                                                   |
+-------------------------------------------------------------------+
```

### 8.3 연결 수립 흐름 (Connection Establishment)

```
+-------------------------------------------------------------------+
|                   Connection Establishment Steps                  |
+-------------------------------------------------------------------+
|                                                                   |
|  zlink_connect("tcp://host:port")                                 |
|       |                                                           |
|       v                                                           |
|  (1) address_t parsing                                            |
|       |  - Identify protocol (tcp, tls, ws, wss, ipc)             |
|       |  - Extract address/port                                   |
|       v                                                           |
|  (2) Create session_base_t                                        |
|       |  - Set reconnection policy                                |
|       v                                                           |
|  (3) Create and start connecter                                   |
|       |  - Select connecter class based on URL scheme             |
|       |  - Call async_connect()                                   |
|       v                                                           |
|  (4) TCP connection complete (3-way handshake)                    |
|       |                                                           |
|       v                                                           |
|  (5) [TLS/WSS] Transport handshake                                |
|       |  - TLS: SSL_do_handshake()                                |
|       |  - WS: HTTP Upgrade request/response                      |
|       v                                                           |
|  (6) Create engine and plug()                                     |
|       |  - Select asio_zmp_engine_t or asio_raw_engine_t          |
|       |    based on socket type                                   |
|       v                                                           |
|  (7) [ZMP] Protocol handshake                                     |
|       |  - HELLO exchange (socket type, Identity)                 |
|       |  - Socket type compatibility check                        |
|       |  - READY exchange (metadata)                              |
|       v                                                           |
|  (8) engine_ready()                                               |
|       - Create and connect pipe                                   |
|       - start_input() / start_output()                            |
|       -> Data send/receive is now possible                        |
|                                                                   |
+-------------------------------------------------------------------+
```

---

## 9. 소스 트리 구조

```
core/
+-- include/                         # Public headers (zlink.h)
|
+-- src/
|   +-- api/                         # Public C API
|   |   +-- zlink.cpp                # Entry point for all zlink_* functions
|   |   +-- zlink_utils.cpp          # Utility functions
|   |
|   +-- core/                        # System base components
|   |   +-- ctx.cpp/hpp              # Context (thread pool, socket management)
|   |   +-- msg.cpp/hpp              # Message container (64B fixed)
|   |   +-- pipe.cpp/hpp             # Lock-free bidirectional pipe
|   |   +-- session_base.cpp/hpp     # Socket-engine bridge
|   |   +-- io_thread.cpp/hpp        # I/O worker thread
|   |   +-- mailbox.cpp/hpp          # Inter-thread command delivery
|   |   +-- object.cpp/hpp           # Base object (command processing)
|   |   +-- own.cpp/hpp              # Ownership management
|   |   +-- reaper.cpp/hpp           # Terminated resource cleanup
|   |   +-- signaler.cpp/hpp         # Thread wake-up signal
|   |   +-- options.cpp/hpp          # Socket option storage
|   |   +-- address.cpp/hpp          # Address parsing
|   |   +-- endpoint.cpp/hpp         # Endpoint management
|   |   +-- command.hpp              # Inter-thread command definitions
|   |   +-- socket_poller.cpp/hpp    # Socket poller
|   |   +-- ...
|   |
|   +-- sockets/                     # Socket type implementations
|   |   +-- socket_base.cpp/hpp      # Base class for all sockets
|   |   +-- pair.cpp/hpp             # PAIR socket
|   |   +-- dealer.cpp/hpp           # DEALER socket
|   |   +-- router.cpp/hpp           # ROUTER socket
|   |   +-- pub.cpp/hpp              # PUB socket
|   |   +-- sub.cpp/hpp              # SUB socket
|   |   +-- xpub.cpp/hpp             # XPUB socket
|   |   +-- xsub.cpp/hpp             # XSUB socket
|   |   +-- stream.cpp/hpp           # STREAM socket
|   |   +-- lb.cpp/hpp               # Load balancer (Round-robin)
|   |   +-- fq.cpp/hpp               # Fair queue (Fair Queueing)
|   |   +-- dist.cpp/hpp             # Distributor (Fan-out)
|   |   +-- proxy.cpp/hpp            # Proxy utility
|   |
|   +-- engine/                      # I/O engines
|   |   +-- i_engine.hpp             # Engine interface
|   |   +-- asio/
|   |       +-- asio_engine.cpp/hpp       # Base Proactor engine
|   |       +-- asio_zmp_engine.cpp/hpp   # ZMP protocol engine
|   |       +-- asio_raw_engine.cpp/hpp   # RAW protocol engine
|   |       +-- asio_poller.cpp/hpp       # io_context wrapper
|   |       +-- i_asio_transport.hpp      # Transport interface
|   |       +-- handler_allocator.hpp     # Handler memory management
|   |       +-- asio_error_handler.hpp    # Error handling
|   |
|   +-- protocol/                    # Protocol encoding/decoding
|   |   +-- zmp_protocol.hpp         # ZMP v1.0 constant definitions
|   |   +-- zmp_encoder.cpp/hpp      # ZMP encoder
|   |   +-- zmp_decoder.cpp/hpp      # ZMP decoder
|   |   +-- zmp_metadata.hpp         # ZMP metadata
|   |   +-- raw_encoder.cpp/hpp      # RAW encoder (no extra framing)
|   |   +-- raw_decoder.cpp/hpp      # RAW decoder
|   |   +-- encoder.hpp              # Encoder base template
|   |   +-- decoder.hpp              # Decoder base template
|   |   +-- i_encoder.hpp            # Encoder interface
|   |   +-- i_decoder.hpp            # Decoder interface
|   |   +-- metadata.cpp/hpp         # Metadata processing
|   |   +-- wire.hpp                 # Byte order conversion
|   |   +-- decoder_allocators.cpp/hpp # Decoder memory management
|   |
|   +-- transports/                  # Transport implementations
|   |   +-- tcp/                     # TCP transport
|   |   |   +-- tcp_transport.cpp/hpp
|   |   |   +-- asio_tcp_connecter.cpp/hpp
|   |   |   +-- asio_tcp_listener.cpp/hpp
|   |   |   +-- tcp_address.cpp/hpp
|   |   |   +-- tcp.cpp/hpp
|   |   |
|   |   +-- ipc/                     # IPC transport (Unix only)
|   |   |   +-- ipc_transport.cpp/hpp
|   |   |   +-- asio_ipc_connecter.cpp/hpp
|   |   |   +-- asio_ipc_listener.cpp/hpp
|   |   |   +-- ipc_address.cpp/hpp
|   |   |
|   |   +-- ws/                      # WebSocket transport (Beast)
|   |   |   +-- ws_transport.cpp/hpp
|   |   |   +-- asio_ws_connecter.cpp/hpp
|   |   |   +-- asio_ws_listener.cpp/hpp
|   |   |   +-- asio_ws_engine.cpp/hpp   # (unused, uses asio_zmp/raw_engine)
|   |   |   +-- ws_address.cpp/hpp
|   |   |
|   |   +-- tls/                     # TLS/SSL transport (OpenSSL)
|   |       +-- ssl_transport.cpp/hpp
|   |       +-- wss_transport.cpp/hpp
|   |       +-- asio_tls_connecter.cpp/hpp
|   |       +-- asio_tls_listener.cpp/hpp
|   |       +-- ssl_context_helper.cpp/hpp
|   |       +-- wss_address.cpp/hpp
|   |
|   +-- services/                    # High-level services
|   |   +-- common/                  # Common service utilities
|   |   |   +-- advertise_endpoint.hpp   # Endpoint resolution for service registration
|   |   |   +-- monitor_decode.hpp       # Monitor event decoding
|   |   |   +-- service_runtime_base.hpp # Service lifecycle kernel
|   |   |   +-- socket_monitor_bridge.hpp # PAIR-based socket monitor bridge
|   |   +-- mesh/                    # 10.0.0 mesh service runtime
|   |       +-- mesh_runtime.cpp/hpp # Object model: mailboxes, ready index, claims, budgets, monitor
|   |       +-- mesh_wire.cpp/hpp    # Node-owned ROUTER wire: ingress thread, admission, envelope
|   |
|   +-- utils/                       # Utilities
|       +-- ypipe.hpp                # Lock-free pipe
|       +-- yqueue.hpp               # Lock-free queue
|       +-- atomic_counter.hpp       # Atomic counter
|       +-- atomic_ptr.hpp           # Atomic pointer
|       +-- blob.hpp                 # Binary blob
|       +-- clock.cpp/hpp            # Time measurement
|       +-- random.cpp/hpp           # Random number generation
|       +-- ip_resolver.cpp/hpp      # IP address resolution
|       +-- mtrie.cpp/hpp            # Multi-trie (XPUB subscriptions)
|       +-- trie.cpp/hpp             # Trie
|       +-- radix_tree.cpp/hpp       # Radix tree (XSUB subscriptions)
|       +-- generic_mtrie.hpp        # Generic multi-trie template
|       +-- mutex.hpp                # Mutex wrapper
|       +-- condition_variable.hpp   # Condition variable wrapper
|       +-- err.cpp/hpp              # Error handling
|       +-- ip.cpp/hpp               # IP utilities
|       +-- config.hpp               # Compile-time configuration
|       +-- ...
|
+-- tests/                           # Functional tests
+-- tests/                           # Internal tests
```

---

## 10. 구조 설계 철학

앞선 절들은 zlink 아키텍처가 **어떻게** 구성되어 있는지 — 계층 구조, 컴포넌트,
데이터 흐름, 소스 트리를 설명했다. 이 절은 **왜** 시스템을 이렇게 구성하는지를
설명한다: 구조 결정을 안내하고 복잡도가 무질서하게 자라는 것을 막는 설계 원칙들이다.

### 10.1 깊은 모듈 — 좁은 인터페이스, 숨겨진 복잡성

좋은 모듈은 좁은 인터페이스 뒤에 많은 복잡성을 숨긴다.
호출자가 모듈을 쓰려고 알아야 할 개념이 적을수록 좋다.

zlink에서 이 원칙은 여러 수준에서 적용된다:

- **Socket runtime**은 endpoint registry, peer state 추적, monitor bridge,
  dispatch bridge, lifecycle quiesce를 흡수하여 — `send`/`recv` 기능,
  `bind`/`connect`/`term` 의미, readiness hook만 노출한다.
- **Engine pipeline**은 speculative I/O, gather write, buffer growth 전략,
  handshake 상태 머신, heartbeat를 흡수하여 — ingress frame delivery,
  egress frame submission, connection state transition만 노출한다.
- **Transport adapter**는 URI 파싱, connect/listen 전략,
  TLS/WS/WSS handshake 상세를 흡수하여 — `client_endpoint`,
  `server_endpoint`, `async_transport_channel`만 노출한다.

**안티패턴: 얕은 분해.** 큰 타입을 작은 helper 여러 개로 쪼개는 것만으로는
호출자가 알아야 할 개념 수가 줄지 않는다. 새 타입이 소비자로부터 복잡성을 숨기지
않는다면 추출은 표면적만 늘릴 뿐이다. 새 타입은 호출자가 알아야 할 개념 수를
줄이거나 hot-path 정책을 한곳에 가둘 때만 정당화된다.

### 10.2 정보 은닉과 소유권 명확성

각 모듈은 내부 구현을 숨기고, 상위 계층은 하위 계층의 세부를 몰라야 한다.

**리소스마다 단일 authoritative close owner 원칙.** 모든 리소스는 수명주기를
책임지는 단일 소유자를 가진다:

| 역할 | 책임 |
| --- | --- |
| Service runtime | Lifecycle coordinator — 시작/종료 순서 조율 |
| Socket runtime | Concrete close owner — 소켓 리소스 보유 및 해제 |
| Reaper | Finalization executor — quiesce 후 지연 정리 수행 |

같은 리소스를 여러 주체가 닫을 수 있으면 shutdown 레이스와 double-free
버그가 생긴다. 목표는 권한 없는 close를 문서로 금지하는 것이 아니라
구조적으로 불가능하게 만드는 것이다.

**정보 누출의 원인.** 내부 구현 세부가 외부로 새는 경로는 두 가지다:

1. *설계 시점 추상화 오류* — 인터페이스가 처음부터 내부 구조를 반영
2. *점진적 인터페이스 팽창* — 새 요구사항마다 내부 세부를 하나씩 노출하여
   인터페이스가 결국 구현을 그대로 비추게 됨

어떤 원인인지 파악해야 대응이 달라진다: (1)이면 인터페이스 재설계가
필요하고, (2)이면 내부 재구성을 하되 이미 내부 개념을 노출한 API 부분도
선택적으로 정리한다.

### 10.3 의미(semantic)와 메커니즘(mechanism) 분리

소켓 계층 안에서 두 관심사는 명확히 분리된다:

- **Socket family** (PAIR, PUB/SUB, DEALER, ROUTER, STREAM)는 메시지
  의미와 라우팅 정책을 소유한다 — 메시지가 무엇을 뜻하고 어디로 가는지.
- **Socket runtime**은 공통 메커니즘을 소유한다 — endpoint registry,
  peer state, monitor bridge, dispatch bridge, lifecycle quiesce — 모든
  소켓이 family와 무관하게 수행하는 기계적 기반 작업.

경계 검증은 양방향이다:

- family 구현이 mechanism 내부에 의존해서는 안 된다.
- mechanism 변경이 family 코드 수정을 요구해서는 안 된다.

이 분리가 유지되면, 새 socket family 추가 시 runtime을 건드리지 않고
runtime 진화(예: monitor 인코딩 변경) 시 어떤 family 구현도 건드리지 않는다.

### 10.4 계층마다 다른 추상화

각 계층은 단순 위임(pass-through)이 아니라 고유한 추상화를 제공해야 한다.
추상화를 더하지 않는 위임 전용 계층은 제거 대상이다.

| 계층 | 제공하는 추상화 |
| --- | --- |
| Service facade | 서비스 의미 (create / attach / destroy / monitor) |
| Service runtime | Lifecycle 상태 머신과 readiness — socket open/close 순서와 drain을 숨김 |
| Engine facade | 연결 수명주기 (start / stop / state) — handshake와 timer를 숨김 |
| Engine pipeline | 비동기 I/O 최적화 — speculative I/O와 buffer 정책을 숨김 |
| Transport adapter | Endpoint open — URI/address/scheme 선택과 TLS/WS/WSS layering을 숨김 |
| Protocol codec | Frame 경계 — wire encoding과 version을 숨김 |

계층화의 목적은 계층을 더 늘리는 것이 아니다. 각 계층이 상위로부터 더 많은
복잡성을 숨기게 만드는 것이다. 계층이 추상화를 변환하지 않고 호출을
그대로 전달하기만 한다면 축소해야 한다.

### 10.5 구조로 오류를 제거한다

정책보다 구조적 보장을 우선한다.

오류를 런타임에 잡는 것이 아니라, 타입 시스템이나 API 설계로 특정 부류의
오류 자체가 발생할 수 없게 만든다:

```
Policy-based:   "Only A should close this resource" (written in docs, code can violate)
Structure-based: Close authority is bound to a type — other actors cannot call close at all
```

구체적 전략:

- `unique_ptr` + move-only 의미론 — 이중 소유가 컴파일 에러가 된다.
- Close guard(sentinel 패턴) — 이중 close가 no-op이 된다.
- RAII wrapper — 수동 close 호출이 구조적으로 불가능하다.

목표는 비용이 합리적인 곳에서 불변 조건을 문서가 아닌 타입 시스템으로
옮기는 것이다.

### 10.6 점진적 복잡도 축적 방어

복잡도는 한 번에 오지 않는다. 개별적으로는 합리적인 작은 변경이 누적되면서
쌓인다 — 하나하나는 정당하지만 종합하면 구조적 명확성을 무너뜨린다.

아키텍처가 방어해야 할 성장 패턴:

- **새 transport 추가** → engine이나 socket 코드에 새 분기가 늘지 않음
- **새 service 추가** → service runtime base에 특수 경로가 늘지 않음
- **새 socket family 추가** → `socket_base_t`를 수정하지 않음

설계 목표: *같은 종류의 기능 추가가 허브 타입을 건드리지 않는 구조.*

이 속성이 유지되면 transport, service, socket family 수가 늘어도 구조의
복잡도는 제한된다. 유지되지 않으면 추가할 때마다 허브 타입이 이해하기 어렵고
수정하기 취약해진다.

### 10.7 성능은 구조적 제약이다

성능은 아키텍처 설계 후에 덧붙이는 것이 아니다.
처음부터 구조 결정을 제약한다.

**Hot-path 정책은 깊은 모듈 안에 둔다.** Speculative I/O, gather write,
buffer growth, zero-copy 경로 같은 최적화는 engine pipeline이나
transport adapter 안에 캡슐화한다. 계층 경계를 넘어 흩뿌리지 않는다.

**성능은 gate이지 trade-off가 아니다.** 구조 변경이 steady-state throughput,
tail latency, CPU 사용률을 악화시키면 채택하지 않는다 — 구조가 객관적으로 더
깔끔해도 마찬가지다. 반대로 성능을 위해 public contract를 약하게 만드는
우회 경로도 금지한다.

두 제약은 서로를 강화한다: 좋은 구조는 hot-path 정책을 격리하여 상위 계층에
누출시키지 않고 최적화하게 하고, 성능 규율은 구조가 현실과 동떨어진
이론적 연습이 되는 것을 막는다.

---

## 부록

### A. 관련 문서

- [ZMP v1.0 프로토콜 상세](protocol-zmp.ko.md)
- [RAW 프로토콜 상세](protocol-raw.ko.md)
- [STREAM 소켓 WS/WSS 최적화](stream-socket.ko.md)
- [스레딩 및 동시성 모델](threading-model.ko.md)
- [연결당 메모리 구조](connection-memory.ko.md)
- [성능 특성 및 튜닝 가이드](../guide/10-performance.ko.md)

### B. 핵심 인터페이스 요약

**i_asio_transport** (모든 트랜스포트의 공통 인터페이스):

```
i_asio_transport
  +-- open(io_context, fd)              Open connection
  +-- close()                           Close connection
  +-- async_read_some(buffer, handler)  Asynchronous read
  +-- async_write_some(buffer, handler) Asynchronous write
  +-- read_some(buffer, size)           Synchronous (speculative) read
  +-- write_some(buffer, size)          Synchronous (speculative) write
  +-- requires_handshake()              Whether handshake is required
  +-- async_handshake(type, handler)    Asynchronous handshake
  +-- is_encrypted()                    Whether encrypted
  +-- supports_speculative_write()      Whether speculative write is supported
  +-- supports_gather_write()           Whether gather write is supported
```

**i_engine** (엔진 인터페이스):

```
i_engine
  +-- plug(session)                     Connect to session
  +-- terminate()                       Terminate
  +-- restart_input()                   Restart receive
  +-- restart_output()                  Restart send
```

### C. 성능 최적화 기법 요약

| 기법                | 설명                                                            |
|--------------------|-----------------------------------------------------------------|
| Speculative I/O    | 비동기 호출 전 동기 I/O를 먼저 시도하여 콜백 오버헤드 제거       |
| Gather Write       | writev()로 헤더+바디를 시스템 콜 1회로 전송                     |
| Zero-Copy Message  | msg_t에 사용자 버퍼 포인터만 저장, 복사 없이 전송               |
| VSM (Inline)       | 41바이트 이하 메시지는 msg_t 내부 버퍼에 직접 저장 (malloc 없음)|
| Lock-free YPipe    | CAS 연산 기반 스레드 간 메시지 교환, 뮤텍스 없음               |
| Cache Line 최적화  | YPipe 노드를 캐시 라인 크기에 맞춰 배치                         |
| Backpressure (배압) | 10MB 한도 초과 시 읽기 중단으로 메모리 폭주 방지                |
