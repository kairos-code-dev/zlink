# zlink System Architecture: Complete Technical Reference

이 문서는 **zlink** 프로젝트의 설계 철학, 시스템 구조, 핵심 컴포넌트, 프로토콜 명세를 상세히 기술합니다.
대상 독자는 서버/네트워크 개발자이며, 처음 접하는 개발자도 시스템을 이해할 수 있도록 구성되었습니다.

---

## 목차

1. [개요 및 설계 철학](#1-개요-및-설계-철학)
2. [시스템 아키텍처 개요](#2-시스템-아키텍처-개요)
3. [계층별 상세 설명](#3-계층별-상세-설명)
4. [핵심 컴포넌트 상세](#4-핵심-컴포넌트-상세)
5. [프로토콜 명세](#5-프로토콜-명세)
6. [데이터 흐름](#6-데이터-흐름)
7. [스레딩 및 동시성 모델](#7-스레딩-및-동시성-모델)
8. [트랜스포트 계층](#8-트랜스포트-계층)
9. [소켓 패턴별 동작](#9-소켓-패턴별-동작)
10. [성능 특성](#10-성능-특성)
11. [소스 트리 구조](#11-소스-트리-구조)

---

## 1. 개요 및 설계 철학

### 1.1 zlink란?

zlink는 Zlink 스타일의 고성능 메시징 라이브러리입니다. 기존 libzlink를 기반으로 하되, 다음과 같은 현대적 설계를 적용했습니다:

- **Boost.Asio 기반 I/O**: 플랫폼별 폴러(epoll/kqueue/IOCP) 대신 Asio의 통합 비동기 I/O 사용
- **WebSocket/TLS 네이티브 지원**: `ws://`, `wss://`, `tls://` 프로토콜 내장
- **간소화된 프로토콜 스택**: ZMTP 대신 자체 ZMP v2.0 프로토콜 사용

### 1.2 설계 원칙

| 원칙 | 설명 |
|------|------|
| **Zero-Copy** | 메시지 복사 최소화를 통한 메모리 대역폭 절약 |
| **Lock-Free** | 스레드 간 통신에 Lock-free 자료구조(YPipe) 사용 |
| **True Async** | Proactor 패턴 기반의 진정한 비동기 I/O |
| **Protocol Agnostic** | 트랜스포트와 프로토콜의 명확한 분리 |

### 1.3 지원 기능

**소켓 패턴**
- PAIR: 1:1 양방향 통신
- PUB/SUB, XPUB/XSUB: 발행-구독 패턴
- DEALER/ROUTER: 비동기 요청-응답 및 라우팅

**트랜스포트**
- `tcp://`: 표준 TCP
- `ipc://`: Unix 도메인 소켓 (Unix/Linux/macOS)
- `inproc://`: 프로세스 내 통신
- `ws://`: WebSocket
- `wss://`: WebSocket over TLS
- `tls://`: 네이티브 TLS

---

## 2. 시스템 아키텍처 개요

### 2.1 계층형 아키텍처

zlink는 5개의 명확히 분리된 계층으로 구성됩니다:

```
┌─────────────────────────────────────────────────────────────────────┐
│                         APPLICATION LAYER                           │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  User Code                                                   │   │
│  │  zlink_ctx_new() → zlink_socket() → zlink_bind/connect()          │   │
│  │  → zlink_send() / zlink_recv() → zlink_close()                    │   │
│  └─────────────────────────────────────────────────────────────┘   │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
┌───────────────────────────────▼─────────────────────────────────────┐
│                          PUBLIC API LAYER                           │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  src/api/zlink.cpp                                            │   │
│  │  - C API 진입점 (zlink_socket, zlink_send, zlink_recv, etc.)      │   │
│  │  - 에러 핸들링 및 파라미터 검증                              │   │
│  └─────────────────────────────────────────────────────────────┘   │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
┌───────────────────────────────▼─────────────────────────────────────┐
│                         SOCKET LOGIC LAYER                          │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  src/sockets/                                                │   │
│  │  - socket_base_t: 모든 소켓의 기반 클래스                    │   │
│  │  - pair_t, dealer_t, router_t, pub_t, sub_t, etc.           │   │
│  │  - 메시지 라우팅 전략 (lb_t, fq_t, dist_t)                   │   │
│  │  - 구독 관리 (XPUB: mtrie_t, XSUB: radix_tree_t/trie_with_size_t) │   │
│  └─────────────────────────────────────────────────────────────┘   │
└───────────────────────────────┬─────────────────────────────────────┘
                                │ (msg_t via pipe_t)
┌───────────────────────────────▼─────────────────────────────────────┐
│                         ENGINE LAYER (ASIO)                         │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  src/engine/asio/                                            │   │
│  │  - asio_engine_t: Proactor 패턴 기반 비동기 I/O 엔진         │   │
│  │  - asio_zmp_engine_t: ZMP 프로토콜 핸드셰이크 및 프레이밍    │   │
│  │  - asio_raw_engine_t: RAW 프로토콜 (Length-Prefix)          │   │
│  └─────────────────────────────────────────────────────────────┘   │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
┌───────────────────────────────▼─────────────────────────────────────┐
│                         PROTOCOL LAYER                              │
│  ┌───────────────────────────┐  ┌─────────────────────────────┐   │
│  │  ZMP v2.0 Protocol        │  │  RAW Protocol               │   │
│  │  src/protocol/zmp_*       │  │  src/protocol/raw_*         │   │
│  │  - 8바이트 고정 헤더       │  │  - 4바이트 길이 접두사       │   │
│  │  - 핸드셰이크 지원         │  │  - 핸드셰이크 없음          │   │
│  └───────────────────────────┘  └─────────────────────────────┘   │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
┌───────────────────────────────▼─────────────────────────────────────┐
│                         TRANSPORT LAYER                             │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  src/transports/                                             │   │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐            │   │
│  │  │   TCP   │ │   IPC   │ │   WS    │ │ TLS/WSS │            │   │
│  │  │tcp_     │ │ipc_     │ │ws_      │ │ssl_     │            │   │
│  │  │transport│ │transport│ │transport│ │transport│            │   │
│  │  └─────────┘ └─────────┘ └─────────┘ └─────────┘            │   │
│  │                                                              │   │
│  │  i_asio_transport: 통합 비동기 인터페이스                    │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

### 2.2 컴포넌트 연결 관계

```
┌──────────────────────────────────────────────────────────────────────┐
│                              ctx_t                                   │
│  (전역 컨텍스트: I/O 스레드풀, 소켓 관리, inproc 엔드포인트)         │
└────────────────────────────────┬─────────────────────────────────────┘
                                 │ owns
            ┌────────────────────┼────────────────────┐
            │                    │                    │
            ▼                    ▼                    ▼
    ┌───────────────┐   ┌───────────────┐   ┌───────────────┐
    │  socket_base_t│   │  io_thread_t  │   │   reaper_t    │
    │  (소켓 인스턴스)│   │ (I/O 워커)    │   │ (자원 정리)   │
    └───────┬───────┘   └───────┬───────┘   └───────────────┘
            │                   │
            │ owns              │ runs
            ▼                   ▼
    ┌───────────────┐   ┌───────────────┐
    │ session_base_t│   │  io_context   │
    │  (세션 관리)   │   │ (Asio 리액터) │
    └───────┬───────┘   └───────────────┘
            │
     ┌──────┴──────┐
     │             │
     ▼             ▼
┌─────────┐  ┌─────────────┐
│ pipe_t  │  │asio_engine_t│
│(메시지큐)│  │ (I/O 엔진)  │
└─────────┘  └──────┬──────┘
                    │
                    ▼
            ┌───────────────┐
            │i_asio_transport│
            │  (트랜스포트)  │
            └───────────────┘
```

---

## 3. 계층별 상세 설명

### 3.1 Application Layer

사용자 코드가 zlink와 상호작용하는 계층입니다.

```c
// 기본 사용 패턴
void *ctx = zlink_ctx_new();                    // 컨텍스트 생성
void *socket = zlink_socket(ctx, ZLINK_DEALER);   // 소켓 생성
zlink_connect(socket, "tcp://127.0.0.1:5555");  // 연결
zlink_send(socket, "Hello", 5, 0);              // 메시지 송신
zlink_recv(socket, buffer, sizeof(buffer), 0); // 메시지 수신
zlink_close(socket);                            // 소켓 닫기
zlink_ctx_term(ctx);                            // 컨텍스트 종료
```

### 3.2 Public API Layer

`src/api/zlink.cpp`에서 모든 공용 C API를 제공합니다.

**주요 함수 그룹**:

| 그룹 | 함수 | 설명 |
|------|------|------|
| Context | `zlink_ctx_new`, `zlink_ctx_term`, `zlink_ctx_set/get` | 컨텍스트 생명주기 |
| Socket | `zlink_socket`, `zlink_close`, `zlink_setsockopt/getsockopt` | 소켓 관리 |
| Connection | `zlink_bind`, `zlink_connect`, `zlink_disconnect`, `zlink_unbind` | 연결 관리 |
| Message | `zlink_send`, `zlink_recv`, `zlink_msg_*` | 메시지 송수신 |
| Polling | `zlink_poll`, `zlink_poller_*` | 이벤트 폴링 |

### 3.3 Socket Logic Layer

각 소켓 타입의 비즈니스 로직을 구현합니다.

**클래스 계층 구조**:

```
socket_base_t (기반 클래스)
├── pair_t              # PAIR 소켓
├── dealer_t            # DEALER 소켓
├── router_t            # ROUTER 소켓 (routing_socket_base_t 상속)
├── xpub_t              # XPUB 소켓
│   └── pub_t           # PUB 소켓 (XPUB 단순화)
├── xsub_t              # XSUB 소켓
│   └── sub_t           # SUB 소켓 (XSUB 단순화)
└── stream_t            # STREAM 소켓
```

**라우팅 전략 클래스**:

| 클래스 | 용도 | 알고리즘 |
|--------|------|----------|
| `lb_t` | 송신측 로드밸런싱 | Round-robin |
| `fq_t` | 수신측 공정 큐 | Fair queueing |
| `dist_t` | 메시지 브로드캐스트 | Fan-out to all |

### 3.4 Engine Layer

Boost.Asio 기반의 비동기 I/O 처리를 담당합니다.

**엔진 타입**:

| 엔진 | 프로토콜 | 트랜스포트 | 특징 |
|------|----------|-----------|------|
| `asio_zmp_engine_t` | ZMP v2.0 | TCP, TLS, IPC, WS, WSS | 핸드셰이크, 8바이트 고정 헤더 |
| `asio_raw_engine_t` | RAW | TCP, TLS, IPC, WS, WSS | 4바이트 길이 접두사, STREAM 소켓용 |

> **Note**: WS/WSS 프로토콜도 `asio_zmp_engine_t` 또는 `asio_raw_engine_t`를 사용하며, WebSocket 프레이밍은 `ws_transport_t`/`wss_transport_t`가 처리합니다.

### 3.5 Transport Layer

물리적 네트워크 전송을 추상화합니다. 모든 트랜스포트는 `i_asio_transport` 인터페이스를 구현합니다.

```cpp
class i_asio_transport {
public:
    // 연결 관리
    virtual bool open(io_context &ctx, fd_t fd) = 0;
    virtual void close() = 0;

    // 비동기 I/O
    virtual void async_read_some(buffer, size, handler) = 0;
    virtual void async_write_some(buffer, size, handler) = 0;

    // 동기(추측적) I/O
    virtual size_t read_some(buffer, size) = 0;
    virtual size_t write_some(buffer, size) = 0;

    // 핸드셰이크 (TLS/WebSocket)
    virtual bool requires_handshake() const { return false; }
    virtual void async_handshake(type, handler);

    // 특성 조회
    virtual bool is_encrypted() const { return false; }
    virtual bool supports_speculative_write() const { return true; }
    virtual bool supports_gather_write() const { return false; }
};
```

---

## 4. 핵심 컴포넌트 상세

### 4.1 msg_t - 메시지 컨테이너

모든 메시지 데이터를 담는 64바이트 고정 크기 구조체입니다.

**메모리 레이아웃**:

```
┌─────────────────────────────────────────────────────────────────┐
│                        msg_t (64 bytes)                         │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  base_t (공통 필드)                                      │   │
│  │  - metadata_t* metadata (8 bytes)                        │   │
│  │  - uint32_t routing_id (4 bytes)                         │   │
│  │  - group_t group (16 bytes)                              │   │
│  │  - uint8_t flags (1 byte)                                │   │
│  │  - uint8_t type (1 byte)                                 │   │
│  └─────────────────────────────────────────────────────────┘   │
│                              OR                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  vsm_t (Very Small Message, ≤max_vsm_size)               │   │
│  │  - max_vsm_size = 64 - (sizeof(ptr) + 3 + 16 + 4)        │   │
│  │  - 64-bit: 33 bytes, 32-bit: 37 bytes                    │   │
│  │  - uint8_t data[max_vsm_size] : 메시지 데이터 직접 저장   │   │
│  │  - uint8_t size : 실제 크기                              │   │
│  └─────────────────────────────────────────────────────────┘   │
│                              OR                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  lmsg_t (Large Message, >max_vsm_size)                   │   │
│  │  - content_t* content : 별도 할당된 버퍼 포인터          │   │
│  │    ├── void* data                                        │   │
│  │    ├── size_t size                                       │   │
│  │    ├── msg_free_fn* ffn (해제 함수)                      │   │
│  │    └── atomic_counter_t refcnt (참조 카운트)             │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

**메시지 플래그**:

| 플래그 | 값 | 설명 |
|--------|-----|------|
| `more` | 0x01 | 멀티파트 메시지의 중간 프레임 |
| `command` | 0x02 | 제어 프레임 (핸드셰이크, 하트비트) |
| `routing_id` | 0x40 | 라우팅 ID 포함 |
| `shared` | 0x80 | 공유 버퍼 (참조 카운팅) |

**메시지 유형**:

| 유형 | 값 | 설명 |
|------|-----|------|
| `type_vsm` | 101 | Very Small Message (≤33 bytes on 64-bit, 복사 없음) |
| `type_lmsg` | 102 | Large Message (malloc'd 버퍼) |
| `type_cmsg` | 104 | Constant Message (상수 데이터 참조) |
| `type_zclmsg` | 105 | Zero-copy Large Message |

### 4.2 pipe_t - Lock-Free 메시지 큐

스레드 간 메시지 전달을 위한 양방향 파이프입니다.

**구조**:

```
┌───────────────────────────────────────────────────────────────┐
│                          pipe_t                               │
├───────────────────────────────────────────────────────────────┤
│                                                               │
│  Thread A (Socket)              Thread B (I/O)                │
│       │                              │                        │
│       │    ┌──────────────────┐     │                        │
│       ├───►│   _out_pipe      │────►│  (송신: Socket → I/O)  │
│       │    │   (YPipe<msg_t>) │     │                        │
│       │    └──────────────────┘     │                        │
│       │                              │                        │
│       │    ┌──────────────────┐     │                        │
│       │◄───│   _in_pipe       │◄────┤  (수신: I/O → Socket)  │
│       │    │   (YPipe<msg_t>) │     │                        │
│       │    └──────────────────┘     │                        │
│                                                               │
│  High Water Mark (HWM): 메시지 큐 크기 제한                   │
│  - _hwm: 아웃바운드 HWM                                       │
│  - _lwm: 인바운드 Low Water Mark (HWM의 절반)                 │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

**YPipe 특성**:
- Lock-free FIFO 큐 (CAS 연산 기반)
- 캐시 라인 최적화
- 메모리 배리어를 통한 가시성 보장

**파이프 상태**:

```
                    ┌────────────┐
                    │   active   │◄──────────────────┐
                    └─────┬──────┘                   │
                          │ receive delimiter        │ connect
                          ▼                          │
              ┌───────────────────────┐              │
              │ delimiter_received    │              │
              └───────────┬───────────┘              │
                          │ send term_ack            │
                          ▼                          │
              ┌───────────────────────┐              │
              │    term_ack_sent      │              │
              └───────────┬───────────┘              │
                          │ receive term_ack         │
                          ▼                          │
                    ┌───────────┐                    │
                    │ terminated│────────────────────┘
                    └───────────┘      (재연결 시)
```

### 4.3 ctx_t - 컨텍스트

전역 상태를 관리하는 최상위 객체입니다.

**주요 역할**:

1. **I/O 스레드 풀 관리**
   - `zlink_ctx_set(ctx, ZLINK_IO_THREADS, n)`으로 스레드 수 설정
   - 각 I/O 스레드는 독립적인 `io_context` 보유

2. **소켓 관리**
   - 소켓 생성/삭제 추적
   - 종료 시 모든 소켓 정리

3. **inproc 엔드포인트 관리**
   - 프로세스 내 통신을 위한 엔드포인트 레지스트리
   - `inproc://name` 형식의 주소 매핑

```cpp
class ctx_t {
private:
    // 소켓 관리
    array_t<socket_base_t> _sockets;      // 활성 소켓들
    std::vector<uint32_t> _empty_slots;   // 빈 슬롯 재사용

    // I/O 스레드
    std::vector<io_thread_t*> _io_threads;
    std::vector<i_mailbox*> _slots;       // 스레드간 메일박스

    // inproc 관리
    std::map<std::string, endpoint_t> _endpoints;
    std::multimap<std::string, pending_connection_t> _pending_connections;

    // 설정
    int _max_sockets;      // 최대 소켓 수 (기본: 1023)
    int _io_thread_count;  // I/O 스레드 수 (기본: 1)
    int _max_msgsz;        // 최대 메시지 크기
};
```

### 4.4 session_base_t - 세션

소켓과 엔진 사이의 브리지 역할을 합니다.

**역할**:
- 메시지 중개: 소켓 ↔ 파이프 ↔ 엔진
- 연결 상태 관리
- 재연결 로직

```
┌─────────────────────────────────────────────────────────────┐
│                     session_base_t                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────┐    ┌─────────┐    ┌─────────────────┐   │
│  │ socket_base_t│◄──►│ pipe_t  │◄──►│ asio_engine_t   │   │
│  │              │    │         │    │                 │   │
│  │  zlink_send()  │    │ YPipe   │    │ async_read/     │   │
│  │  zlink_recv()  │    │         │    │ async_write     │   │
│  └──────────────┘    └─────────┘    └─────────────────┘   │
│                                                             │
│  push_msg(): 엔진 → 세션 → 파이프 → 소켓                   │
│  pull_msg(): 소켓 → 파이프 → 세션 → 엔진                   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 4.5 asio_engine_t - I/O 엔진

Boost.Asio 기반의 비동기 I/O를 처리하는 핵심 엔진입니다.

**Proactor 패턴**:

```
┌──────────────────────────────────────────────────────────────────┐
│                        asio_engine_t                             │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────────────┐         ┌──────────────────────────────┐  │
│  │ async_read_some │────────►│      on_read_complete        │  │
│  │   (비동기 읽기)  │         │  - 데이터 수신 완료 콜백       │  │
│  └─────────────────┘         │  - 디코더로 메시지 파싱        │  │
│                              │  - 세션으로 메시지 전달        │  │
│                              └──────────────────────────────┘  │
│                                                                  │
│  ┌─────────────────┐         ┌──────────────────────────────┐  │
│  │async_write_some │────────►│     on_write_complete        │  │
│  │   (비동기 쓰기)  │         │  - 데이터 송신 완료 콜백       │  │
│  └─────────────────┘         │  - 다음 메시지 인코딩          │  │
│                              │  - 더 보낼 데이터 있으면 반복   │  │
│                              └──────────────────────────────┘  │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                  Speculative I/O (최적화)                │   │
│  │  - speculative_read(): 즉시 읽을 수 있는 데이터 동기 읽기  │   │
│  │  - speculative_write(): 즉시 쓸 수 있으면 동기 쓰기       │   │
│  │  → 비동기 오버헤드 없이 처리량 향상                        │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                   Backpressure (배압)                    │   │
│  │  - _pending_buffers: 처리 못한 데이터 임시 저장           │   │
│  │  - max_pending_buffer_size: 10MB 제한                    │   │
│  │  - 제한 초과 시 읽기 중단, 이후 재개                       │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

**엔진 상태 머신**:

```
          ┌─────────────────────┐
          │    생성 (Created)    │
          └──────────┬──────────┘
                     │ plug()
                     ▼
          ┌─────────────────────┐
          │   핸드셰이크 중      │ (TLS/WebSocket은 트랜스포트 핸드셰이크)
          │  (_handshaking)     │ (ZMP는 프로토콜 핸드셰이크)
          └──────────┬──────────┘
                     │ handshake 완료
                     ▼
          ┌─────────────────────┐
          │    활성 (Active)    │◄─────────────────┐
          │   데이터 송수신      │                  │
          └──────────┬──────────┘                  │
                     │ I/O 에러                     │ restart
                     ▼                             │
          ┌─────────────────────┐                  │
          │    에러 (Error)      │─────────────────┘
          └──────────┬──────────┘
                     │ terminate()
                     ▼
          ┌─────────────────────┐
          │   종료 (Terminated)  │
          └─────────────────────┘
```

---

## 5. 프로토콜 명세

### 5.1 ZMP v2.0 (Zlink Message Protocol)

zlink 노드 간 통신에 사용되는 자체 프로토콜입니다.

**설계 원칙**:
- ZMTP 비호환 (zlink 전용 최적화)
- 8바이트 고정 헤더 (가변 길이 인코딩 배제)
- 최소한의 핸드셰이크

**프레임 헤더 구조**:

```
 Byte:   0         1         2         3         4    5    6    7
      ┌─────────┬─────────┬─────────┬─────────┬─────────────────────┐
      │  MAGIC  │ VERSION │  FLAGS  │RESERVED │   PAYLOAD SIZE      │
      │  (0x5A) │  (0x02) │         │ (0x00)  │   (32-bit BE)       │
      └─────────┴─────────┴─────────┴─────────┴─────────────────────┘
```

**필드 설명**:

| 필드 | 오프셋 | 크기 | 설명 |
|------|--------|------|------|
| MAGIC | 0 | 1 | 매직 넘버 `0x5A` ('Z') |
| VERSION | 1 | 1 | 프로토콜 버전 `0x02` |
| FLAGS | 2 | 1 | 프레임 플래그 |
| RESERVED | 3 | 1 | 예약 (0x00) |
| PAYLOAD SIZE | 4-7 | 4 | 페이로드 크기 (Big Endian) |

**FLAGS 비트 정의**:

| 비트 | 이름 | 설명 |
|------|------|------|
| 0 | MORE | 멀티파트 메시지 계속 |
| 1 | CONTROL | 제어 프레임 |
| 2 | IDENTITY | 라우팅 ID 포함 |
| 3 | SUBSCRIBE | 구독 요청 |
| 4 | CANCEL | 구독 취소 |

**핸드셰이크 시퀀스**:

```
    Client                              Server
       │                                   │
       │─────── HELLO (greeting) ─────────►│
       │                                   │
       │◄────── HELLO (greeting) ──────────│
       │                                   │
       │                                   │  (소켓 타입 호환성 검사)
       │                                   │
       │─────── READY (metadata) ─────────►│
       │                                   │
       │◄────── READY (metadata) ──────────│
       │                                   │
       │◄─────── Data Exchange ───────────►│
       │                                   │
```

**HELLO 프레임 내용**:
- 소켓 타입 (1 byte)
- Identity 길이 (1 byte)
- Identity 값 (0-255 bytes, DEALER/ROUTER만 사용)

**READY 프레임 내용** (`zmp_metadata::add_basic_properties`):
- Socket-Type 속성 (항상 포함)
- Identity 속성 (DEALER/ROUTER만 포함)

### 5.2 RAW Protocol (Length-Prefixed)

STREAM 소켓 및 외부 클라이언트 연동용 단순 프로토콜입니다.

**Wire Format**:

```
┌──────────────────────┬─────────────────────────────┐
│  Length (4 Bytes)    │     Payload (N Bytes)       │
│  (Big Endian)        │                             │
└──────────────────────┴─────────────────────────────┘
```

**특징**:
- 핸드셰이크 없음 (즉시 데이터 송수신)
- 간단한 구현 (`read(4) → read(length)`)
- 외부 클라이언트 연동 용이

**STREAM 소켓 내부 API** (zlink_send/zlink_recv 멀티파트):

```
송신 (zlink_send):
  Frame 1: [Routing ID (4 bytes)]  + MORE flag
  Frame 2: [Payload (N bytes)]

수신 (zlink_recv):
  Frame 1: [Routing ID (4 bytes)]  + MORE flag
  Frame 2: [Payload (N bytes)]

이벤트 메시지 (Payload가 1 byte인 경우):
- Connect:    [Routing ID] + MORE, [0x01]
- Disconnect: [Routing ID] + MORE, [0x00]
```

> **Note**: 단일 프레임이 아닌 멀티파트 메시지입니다.
> 애플리케이션은 `zlink_recv`를 두 번 호출하여 routing_id와 payload를 각각 수신합니다.

### 5.3 WebSocket 프레이밍

WebSocket 트랜스포트는 Beast 라이브러리를 사용하며, 바이너리 프레임으로 메시지를 전송합니다.

```
┌─────────────────────────────────────────────────────────────┐
│                  WebSocket Frame (RFC 6455)                 │
├─────────────────────────────────────────────────────────────┤
│  FIN=1, Opcode=0x02 (Binary)                                │
│  Payload = ZMP Frame 또는 RAW Frame                         │
└─────────────────────────────────────────────────────────────┘
```

---

## 6. 데이터 흐름

### 6.1 메시지 송신 (Outbound / Tx)

```
┌───────────────────────────────────────────────────────────────────┐
│                    APPLICATION THREAD                             │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  (1) zlink_send(socket, data, size, flags)                         │
│       │                                                           │
│       ▼                                                           │
│  (2) socket_base_t::send()                                       │
│       │  - msg_t 생성 (VSM 또는 LMSG)                             │
│       │  - 소켓 타입별 라우팅 전략 선택                            │
│       │    · DEALER: lb_t (Round-robin)                          │
│       │    · ROUTER: ID 기반 직접 라우팅                          │
│       │    · PUB: dist_t (모든 구독자에게 전송)                   │
│       ▼                                                           │
│  (3) pipe_t::write()                                             │
│       │  - YPipe에 메시지 푸시 (Lock-free)                        │
│       │  - HWM 체크                                               │
│       ▼                                                           │
│  (4) mailbox signal to I/O thread                                │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌───────────────────────────────────────────────────────────────────┐
│                      I/O THREAD                                   │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  (5) asio_engine_t: 이벤트 수신                                   │
│       │                                                           │
│       ▼                                                           │
│  (6) pull_msg_from_session()                                     │
│       │  - 파이프에서 메시지 읽기                                  │
│       ▼                                                           │
│  (7) encoder: 메시지 → 바이트 스트림                              │
│       │  - ZMP: 8바이트 헤더 + 페이로드                           │
│       │  - RAW: 4바이트 길이 + 페이로드                           │
│       ▼                                                           │
│  (8) speculative_write() 시도                                    │
│       │  - 성공: 즉시 동기 쓰기 완료                               │
│       │  - 실패 (EAGAIN): async_write_some() 스케줄              │
│       ▼                                                           │
│  (9) transport: 네트워크 전송                                     │
│       - TCP: 직접 전송                                            │
│       - TLS: SSL 암호화 후 전송                                   │
│       - WS: Beast 프레이밍 후 전송                                │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

### 6.2 메시지 수신 (Inbound / Rx)

```
┌───────────────────────────────────────────────────────────────────┐
│                      I/O THREAD                                   │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  (1) async_read_some() 완료 콜백                                  │
│       │  - 네트워크에서 바이트 수신                                │
│       ▼                                                           │
│  (2) on_read_complete()                                          │
│       │                                                           │
│       ▼                                                           │
│  (3) decoder: 바이트 스트림 → 메시지                              │
│       │  - 헤더 파싱 (ZMP 8B / RAW 4B)                            │
│       │  - 페이로드 크기 확인                                      │
│       │  - msg_t 생성                                             │
│       ▼                                                           │
│  (4) push_msg_to_session()                                       │
│       │                                                           │
│       ▼                                                           │
│  (5) session_base_t::push_msg()                                  │
│       │  - 메시지 검증                                            │
│       │  - 파이프로 전달                                          │
│       ▼                                                           │
│  (6) pipe_t::write() (인바운드 파이프)                            │
│       │  - YPipe에 메시지 푸시                                    │
│       ▼                                                           │
│  (7) 소켓에 읽기 가능 신호                                        │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌───────────────────────────────────────────────────────────────────┐
│                    APPLICATION THREAD                             │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  (8) zlink_recv(socket, buffer, size, flags)                       │
│       │                                                           │
│       ▼                                                           │
│  (9) socket_base_t::recv()                                       │
│       │  - 소켓 타입별 수신 전략                                   │
│       │    · DEALER/SUB: fq_t (Fair Queueing)                    │
│       │    · ROUTER: ID 추출 후 메시지 전달                       │
│       │  - 토픽 필터링 (SUB)                                      │
│       ▼                                                           │
│  (10) pipe_t::read()                                             │
│        │  - YPipe에서 메시지 팝                                   │
│        ▼                                                          │
│  (11) 사용자 버퍼로 데이터 복사                                   │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

### 6.3 핸드셰이크 흐름 (연결 수립)

```
┌───────────────────────────────────────────────────────────────────┐
│                     CONNECTION ESTABLISHMENT                      │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  zlink_connect("tcp://host:port")                                  │
│       │                                                           │
│       ▼                                                           │
│  (1) address_t 파싱                                               │
│       │  - 프로토콜 식별 (tcp, tls, ws, wss)                      │
│       │  - 주소/포트 추출                                         │
│       ▼                                                           │
│  (2) session_base_t 생성                                         │
│       │  - 재연결 정책 설정                                       │
│       ▼                                                           │
│  (3) connecter 생성 및 시작                                       │
│       │  - tcp_connecter_t / ws_connecter_t / ...                │
│       │  - async_connect() 호출                                   │
│       ▼                                                           │
│  (4) TCP 연결 완료                                                │
│       │                                                           │
│       ▼                                                           │
│  (5) [TLS/WSS] 트랜스포트 핸드셰이크                              │
│       │  - TLS: SSL_do_handshake()                               │
│       │  - WS: HTTP Upgrade 요청                                 │
│       ▼                                                           │
│  (6) Engine 생성 및 plug()                                       │
│       │  - asio_zmp_engine_t 또는 asio_raw_engine_t              │
│       ▼                                                           │
│  (7) [ZMP] 프로토콜 핸드셰이크                                    │
│       │  - HELLO 교환                                            │
│       │  - 소켓 타입 호환성 검사                                   │
│       │  - READY 교환                                            │
│       ▼                                                           │
│  (8) 연결 완료, 데이터 송수신 가능                                │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

---

## 7. 스레딩 및 동시성 모델

### 7.1 스레드 구조

```
┌─────────────────────────────────────────────────────────────────┐
│                    zlink Threading Model                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                 Application Threads                      │   │
│  │  - zlink_send() / zlink_recv() 호출                          │   │
│  │  - 소켓별로 하나의 스레드에서만 접근 권장                  │   │
│  │  - 여러 소켓은 여러 스레드에서 사용 가능                   │   │
│  └─────────────────────────────────────────────────────────┘   │
│                              │                                  │
│                   Lock-free Pipes (YPipe)                       │
│                              │                                  │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    I/O Threads                           │   │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐                │   │
│  │  │ Thread 0 │ │ Thread 1 │ │ Thread N │  (설정 가능)    │   │
│  │  │io_context│ │io_context│ │io_context│                │   │
│  │  └──────────┘ └──────────┘ └──────────┘                │   │
│  │                                                          │   │
│  │  - 비동기 I/O 처리                                       │   │
│  │  - 인코더/디코더 실행                                    │   │
│  │  - 네트워크 송수신                                       │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    Reaper Thread                         │   │
│  │  - 종료된 소켓/세션 자원 정리                             │   │
│  │  - 지연된 삭제 처리                                      │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 7.2 스레드 간 통신

**Mailbox 시스템**:

```cpp
// 각 스레드는 자신만의 mailbox를 가짐
class mailbox_t {
    ypipe_t<command_t> _commands;  // 명령 큐
    signaler_t _signaler;           // 깨우기 신호

    void send(const command_t &cmd);
    int recv(command_t *cmd, int timeout);
};

// 명령 타입
enum command_type {
    stop,           // 스레드 종료
    plug,           // 엔진 연결
    attach,         // 세션에 엔진 연결
    bind,           // 바인드 완료
    activate_read,  // 읽기 활성화
    activate_write, // 쓰기 활성화
    // ...
};
```

**스레드 간 데이터 흐름**:

```
Application Thread              I/O Thread
      │                              │
      │  zlink_send()                  │
      │      │                       │
      │      ▼                       │
      │  [msg_t를 YPipe에 push]      │
      │      │                       │
      │  mailbox.send(activate_write)│
      │─────────────────────────────►│
      │                              │  (신호 수신)
      │                              │
      │                              ▼
      │                         [YPipe에서 msg_t pop]
      │                              │
      │                         [인코딩 및 전송]
```

### 7.3 I/O 스레드 선택 (로드 밸런싱)

```cpp
// 새 연결 시 I/O 스레드 선택
io_thread_t* ctx_t::choose_io_thread(uint64_t affinity) {
    // affinity 마스크에 맞는 스레드 중
    // 가장 적은 부하를 가진 스레드 선택
    int min_load = INT_MAX;
    io_thread_t *best = nullptr;

    for (auto thread : _io_threads) {
        if (affinity & (1 << thread->id())) {
            if (thread->load() < min_load) {
                min_load = thread->load();
                best = thread;
            }
        }
    }
    return best;
}
```

---

## 8. 트랜스포트 계층

### 8.1 트랜스포트 비교

| 트랜스포트 | 핸드셰이크 | 암호화 | Speculative Write | Gather Write | 용도 |
|-----------|:----------:|:------:|:-----------------:|:------------:|------|
| TCP | ✗ | ✗ | ✓ | ✓ | 표준 네트워크 통신 |
| IPC | ✗ | ✗ | △ (옵션) | ✓ | 로컬 프로세스 간 통신 |
| TLS | ✓ | ✓ | ✗ | ✗ | 암호화된 네트워크 통신 |
| WS | ✓ | ✗ | ✗ | ✓ | 웹 클라이언트 연동 |
| WSS | ✓ | ✓ | ✗ | ✓ | 암호화된 웹 클라이언트 연동 |

### 8.2 TCP 트랜스포트

```cpp
class tcp_transport_t : public i_asio_transport {
private:
    std::unique_ptr<boost::asio::ip::tcp::socket> _socket;

public:
    // 논블로킹 소켓 설정
    bool open(io_context &ctx, fd_t fd) {
        _socket = make_unique<tcp::socket>(ctx);
        _socket->assign(tcp::v4(), fd);
        _socket->non_blocking(true);

        // TCP_NODELAY 설정 (Nagle 알고리즘 비활성화)
        _socket->set_option(tcp::no_delay(true));
        return true;
    }

    // Speculative write 지원
    bool supports_speculative_write() const { return true; }

    // Gather write 지원 (헤더+바디 한번에 전송)
    bool supports_gather_write() const { return true; }
};
```

### 8.3 TLS 트랜스포트

```cpp
class ssl_transport_t : public i_asio_transport {
private:
    boost::asio::ssl::context &_ssl_ctx;
    std::unique_ptr<ssl::stream<tcp::socket>> _ssl_stream;

public:
    bool requires_handshake() const { return true; }
    bool is_encrypted() const { return true; }

    void async_handshake(int type, handler_t handler) {
        auto hs_type = (type == 0)
            ? ssl::stream_base::client
            : ssl::stream_base::server;
        _ssl_stream->async_handshake(hs_type, handler);
    }
};
```

**TLS 옵션**:

| 옵션 | 설명 |
|------|------|
| `ZLINK_TLS_CERT` | 인증서 파일 경로 (PEM) |
| `ZLINK_TLS_KEY` | 개인키 파일 경로 (PEM) |
| `ZLINK_TLS_CA` | CA 인증서 경로 |
| `ZLINK_TLS_HOSTNAME` | 서버 호스트명 (인증서 검증용) |

### 8.4 WebSocket 트랜스포트

```cpp
class ws_transport_t : public i_asio_transport {
private:
    using ws_stream_t = beast::websocket::stream<tcp::socket>;
    std::unique_ptr<ws_stream_t> _ws_stream;
    beast::flat_buffer _read_buffer;  // 프레임 버퍼

public:
    bool open(io_context &ctx, fd_t fd) {
        _ws_stream = make_unique<ws_stream_t>(ctx);
        _ws_stream->binary(true);  // 바이너리 모드
        _ws_stream->write_buffer_bytes(64 * 1024);  // 64KB 버퍼
        _ws_stream->auto_fragment(false);  // 프래그먼트 비활성화
        return true;
    }

    bool requires_handshake() const { return true; }

    // WebSocket은 프레임 기반이라 speculative write 미지원
    bool supports_speculative_write() const { return false; }
};
```

### 8.5 IPC 트랜스포트

```cpp
class ipc_transport_t : public i_asio_transport {
private:
    using unix_socket_t = boost::asio::local::stream_protocol::socket;
    std::unique_ptr<unix_socket_t> _socket;

public:
    // Unix/Linux/macOS에서만 사용 가능
    #if defined ZLINK_HAVE_IPC

    bool supports_speculative_write() const {
        // 환경 변수로 제어 (안정성 vs 성능)
        // ZLINK_ASIO_IPC_SYNC_WRITE=1: speculative write 활성화
        return _enable_speculative;
    }

    #endif
};
```

### 8.6 프로토콜-트랜스포트-엔진 매핑

zlink는 프로토콜(URL 스킴), 트랜스포트, 엔진을 계층적으로 분리하여 다양한 조합을 지원합니다.

#### 8.6.1 계층 구조 개요

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        연결 수립 계층 구조                               │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  1. Protocol Layer (URL 스킴)                                    │   │
│  │     tcp://  tls://  ws://  wss://  ipc://  inproc://             │   │
│  └───────────────────────────┬─────────────────────────────────────┘   │
│                              │ 주소 파싱                                │
│                              ▼                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  2. Connecter Layer (연결 수립)                                  │   │
│  │     asio_tcp_connecter_t                                         │   │
│  │     asio_tls_connecter_t                                         │   │
│  │     asio_ws_connecter_t  (ws:// 와 wss:// 모두 처리)             │   │
│  │     asio_ipc_connecter_t                                         │   │
│  └───────────────────────────┬─────────────────────────────────────┘   │
│                              │ TCP 연결 완료                            │
│                              ▼                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  3. Transport Layer (I/O 추상화)                                 │   │
│  │     tcp_transport_t   → 평문 TCP                                 │   │
│  │     ssl_transport_t   → TLS 암호화                               │   │
│  │     ws_transport_t    → WebSocket 프레이밍                       │   │
│  │     wss_transport_t   → WebSocket + TLS                         │   │
│  │     ipc_transport_t   → Unix 도메인 소켓                         │   │
│  └───────────────────────────┬─────────────────────────────────────┘   │
│                              │ 트랜스포트 핸드셰이크 (TLS/WS)           │
│                              ▼                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │  4. Engine Layer (메시지 프로토콜)                               │   │
│  │     asio_zmp_engine_t  → ZMP 프로토콜 (PAIR, DEALER, ROUTER 등) │   │
│  │     asio_raw_engine_t  → RAW 프로토콜 (STREAM 소켓 전용)        │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

#### 8.6.2 프로토콜별 Connecter 선택

`session_base_t::start_connecting()`에서 URL 스킴에 따라 Connecter를 선택합니다:

```cpp
// src/core/session_base.cpp
void session_base_t::start_connecting(bool wait_) {
    if (_addr->protocol == protocol_name::tcp) {
        connecter = new asio_tcp_connecter_t(...);
    }
    else if (_addr->protocol == protocol_name::tls) {
        connecter = new asio_tls_connecter_t(...);
    }
    else if (_addr->protocol == protocol_name::ipc) {
        connecter = new asio_ipc_connecter_t(...);
    }
    else if (_addr->protocol == protocol_name::ws
             || _addr->protocol == protocol_name::wss) {
        // WS와 WSS 모두 같은 Connecter 사용
        connecter = new asio_ws_connecter_t(...);
    }
}
```

| 프로토콜 | Connecter 클래스 | 소스 파일 |
|---------|-----------------|----------|
| `tcp://` | `asio_tcp_connecter_t` | `src/transports/tcp/asio_tcp_connecter.cpp` |
| `tls://` | `asio_tls_connecter_t` | `src/transports/tls/asio_tls_connecter.cpp` |
| `ws://` | `asio_ws_connecter_t` | `src/transports/ws/asio_ws_connecter.cpp` |
| `wss://` | `asio_ws_connecter_t` | `src/transports/ws/asio_ws_connecter.cpp` |
| `ipc://` | `asio_ipc_connecter_t` | `src/transports/ipc/asio_ipc_connecter.cpp` |

#### 8.6.3 소켓 타입에 따른 엔진 선택

각 Connecter의 `create_engine()` 함수에서 **소켓 타입**에 따라 엔진을 선택합니다:

```cpp
// 모든 Connecter에서 동일한 패턴 적용
void create_engine(fd_t fd_, ...) {
    if (options.type == ZLINK_STREAM) {
        // STREAM 소켓: RAW 프로토콜 (핸드셰이크 없음)
        engine = new asio_raw_engine_t(fd_, options, endpoint_pair, transport);
    } else {
        // 기타 소켓: ZMP 프로토콜 (HELLO/READY 핸드셰이크)
        engine = new asio_zmp_engine_t(fd_, options, endpoint_pair, transport);
    }
}
```

**엔진 선택 규칙**:

| 소켓 타입 | 엔진 | 프로토콜 핸드셰이크 |
|---------|------|-------------------|
| PAIR, DEALER, ROUTER, PUB, SUB, XPUB, XSUB | `asio_zmp_engine_t` | ZMP HELLO/READY |
| STREAM | `asio_raw_engine_t` | 없음 (즉시 데이터 전송) |

#### 8.6.4 전체 매핑 매트릭스

**TCP (tcp://)**:

| 소켓 타입 | Connecter | Transport | Engine | 핸드셰이크 |
|---------|-----------|-----------|--------|----------|
| PAIR/DEALER/ROUTER/PUB/SUB | `asio_tcp_connecter_t` | `tcp_transport_t` | `asio_zmp_engine_t` | ZMP |
| STREAM | `asio_tcp_connecter_t` | `tcp_transport_t` | `asio_raw_engine_t` | 없음 |

**TLS (tls://)**:

| 소켓 타입 | Connecter | Transport | Engine | 핸드셰이크 |
|---------|-----------|-----------|--------|----------|
| PAIR/DEALER/ROUTER/PUB/SUB | `asio_tls_connecter_t` | `ssl_transport_t` | `asio_zmp_engine_t` | SSL → ZMP |
| STREAM | `asio_tls_connecter_t` | `ssl_transport_t` | `asio_raw_engine_t` | SSL만 |

**WebSocket (ws://)**:

| 소켓 타입 | Connecter | Transport | Engine | 핸드셰이크 |
|---------|-----------|-----------|--------|----------|
| PAIR/DEALER/ROUTER/PUB/SUB | `asio_ws_connecter_t` | `ws_transport_t` | `asio_zmp_engine_t` | WS → ZMP |
| STREAM | `asio_ws_connecter_t` | `ws_transport_t` | `asio_raw_engine_t` | WS만 |

**Secure WebSocket (wss://)**:

| 소켓 타입 | Connecter | Transport | Engine | 핸드셰이크 |
|---------|-----------|-----------|--------|----------|
| PAIR/DEALER/ROUTER/PUB/SUB | `asio_ws_connecter_t` | `wss_transport_t` | `asio_zmp_engine_t` | SSL → WS → ZMP |
| STREAM | `asio_ws_connecter_t` | `wss_transport_t` | `asio_raw_engine_t` | SSL → WS |

**IPC (ipc://)** *(Unix/Linux/macOS 전용)*:

| 소켓 타입 | Connecter | Transport | Engine | 핸드셰이크 |
|---------|-----------|-----------|--------|----------|
| PAIR/DEALER/ROUTER/PUB/SUB | `asio_ipc_connecter_t` | `ipc_transport_t` | `asio_zmp_engine_t` | ZMP |
| STREAM | `asio_ipc_connecter_t` | `ipc_transport_t` | `asio_raw_engine_t` | 없음 |

### 8.7 연결 수립 흐름 상세

#### 8.7.1 TCP + DEALER 소켓 연결

```
zlink_connect(socket, "tcp://127.0.0.1:5555")
    │
    ▼
┌─────────────────────────────────────────────────────────────────────┐
│ session_base_t::start_connecting()                                  │
│   protocol == tcp → new asio_tcp_connecter_t()                      │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│ asio_tcp_connecter_t::start_connecting()                            │
│   boost::asio::async_connect()                                      │
└───────────────────────────────┬─────────────────────────────────────┘
                                │ TCP 3-way handshake 완료
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│ asio_tcp_connecter_t::on_connect()                                  │
│   create_engine():                                                   │
│     transport = new tcp_transport_t()                               │
│     socket_type == DEALER (not STREAM)                              │
│     engine = new asio_zmp_engine_t(fd, options, transport)          │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│ session_base_t::process_attach(engine)                              │
│   engine->plug()                                                     │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│ asio_zmp_engine_t::plug()                                           │
│   (트랜스포트 핸드셰이크 불필요 - TCP)                                │
│   handshake() 시작                                                   │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│ ZMP Protocol Handshake                                              │
│   1. send HELLO (소켓 타입, Identity)                               │
│   2. recv HELLO (피어 소켓 타입)                                     │
│   3. 소켓 타입 호환성 검사                                           │
│   4. send READY (메타데이터)                                         │
│   5. recv READY (피어 메타데이터)                                    │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│ engine_ready()                                                       │
│   pipe 생성 및 연결                                                  │
│   start_input() / start_output()                                    │
│   → 데이터 송수신 가능                                               │
└─────────────────────────────────────────────────────────────────────┘
```

#### 8.7.2 WSS + STREAM 소켓 연결

```
zlink_connect(socket, "wss://server.example.com:443/path")
    │
    ▼
┌─────────────────────────────────────────────────────────────────────┐
│ session_base_t::start_connecting()                                  │
│   protocol == wss → new asio_ws_connecter_t(secure=true)           │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│ asio_ws_connecter_t::start_connecting()                             │
│   boost::asio::async_connect()                                      │
└───────────────────────────────┬─────────────────────────────────────┘
                                │ TCP 연결 완료
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│ asio_ws_connecter_t::on_connect()                                   │
│   create_engine():                                                   │
│     _secure == true:                                                │
│       ssl_context = create_wss_client_context(options)              │
│       transport = new wss_transport_t(ssl_context, path, host)      │
│     socket_type == STREAM:                                          │
│       engine = new asio_raw_engine_t(fd, options, transport,        │
│                                      ssl_context)                   │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│ asio_raw_engine_t::plug()                                           │
│   transport->requires_handshake() == true                           │
│   start_transport_handshake()                                       │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│ wss_transport_t::async_handshake()                                  │
│   1. SSL/TLS Handshake (인증서 검증)                                │
│   2. WebSocket HTTP Upgrade:                                        │
│      GET /path HTTP/1.1                                             │
│      Host: server.example.com                                       │
│      Upgrade: websocket                                             │
│      Connection: Upgrade                                            │
│      Sec-WebSocket-Key: ...                                         │
│                                                                     │
│      HTTP/1.1 101 Switching Protocols                               │
│      Upgrade: websocket                                             │
│      Connection: Upgrade                                            │
│      Sec-WebSocket-Accept: ...                                      │
└───────────────────────────────┬─────────────────────────────────────┘
                                │ 트랜스포트 핸드셰이크 완료
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│ asio_raw_engine_t (STREAM 소켓)                                     │
│   ZMP 핸드셰이크 없음 (raw 모드)                                     │
│   engine_ready() 즉시 호출                                          │
│   → 데이터 송수신 가능 (Length-Prefix 프레이밍)                       │
└─────────────────────────────────────────────────────────────────────┘
```

### 8.8 핸드셰이크 단계 요약

각 프로토콜+소켓 조합별 핸드셰이크 단계:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        핸드셰이크 단계 비교                              │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  TCP + PAIR/DEALER/ROUTER                                              │
│  ┌─────────┐    ┌─────────────┐                                       │
│  │  TCP    │───►│  ZMP        │───► 데이터 전송                        │
│  │ Connect │    │  Handshake  │                                       │
│  └─────────┘    └─────────────┘                                       │
│                                                                         │
│  TCP + STREAM                                                          │
│  ┌─────────┐                                                           │
│  │  TCP    │───────────────────────► 데이터 전송 (즉시)                 │
│  │ Connect │                                                           │
│  └─────────┘                                                           │
│                                                                         │
│  TLS + PAIR/DEALER/ROUTER                                              │
│  ┌─────────┐    ┌─────────┐    ┌─────────────┐                        │
│  │  TCP    │───►│  SSL    │───►│  ZMP        │───► 데이터 전송         │
│  │ Connect │    │Handshake│    │  Handshake  │                        │
│  └─────────┘    └─────────┘    └─────────────┘                        │
│                                                                         │
│  TLS + STREAM                                                          │
│  ┌─────────┐    ┌─────────┐                                           │
│  │  TCP    │───►│  SSL    │───────────────────► 데이터 전송            │
│  │ Connect │    │Handshake│                                           │
│  └─────────┘    └─────────┘                                           │
│                                                                         │
│  WS + PAIR/DEALER/ROUTER                                               │
│  ┌─────────┐    ┌─────────┐    ┌─────────────┐                        │
│  │  TCP    │───►│   WS    │───►│  ZMP        │───► 데이터 전송         │
│  │ Connect │    │ Upgrade │    │  Handshake  │                        │
│  └─────────┘    └─────────┘    └─────────────┘                        │
│                                                                         │
│  WSS + PAIR/DEALER/ROUTER                                              │
│  ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────────┐        │
│  │  TCP    │───►│  SSL    │───►│   WS    │───►│  ZMP        │───► 전송│
│  │ Connect │    │Handshake│    │ Upgrade │    │  Handshake  │        │
│  └─────────┘    └─────────┘    └─────────┘    └─────────────┘        │
│                                                                         │
│  WSS + STREAM                                                          │
│  ┌─────────┐    ┌─────────┐    ┌─────────┐                            │
│  │  TCP    │───►│  SSL    │───►│   WS    │───────────────► 데이터 전송 │
│  │ Connect │    │Handshake│    │ Upgrade │                            │
│  └─────────┘    └─────────┘    └─────────┘                            │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 8.9 Listener 측 매핑 (서버)

서버(bind) 측도 동일한 매핑 규칙을 따릅니다:

```cpp
// src/core/socket_base.cpp - bind() 시
if (_addr->protocol == protocol_name::tcp) {
    listener = new asio_tcp_listener_t(...);
}
else if (_addr->protocol == protocol_name::tls) {
    listener = new asio_tls_listener_t(...);
}
else if (_addr->protocol == protocol_name::ws
         || _addr->protocol == protocol_name::wss) {
    listener = new asio_ws_listener_t(...);
}
```

**Listener에서 accept 후 엔진 생성**:

```cpp
// 예: asio_tcp_listener_t::on_accept()
void on_accept(fd_t fd_) {
    if (options.type == ZLINK_STREAM)
        engine = new asio_raw_engine_t(fd_, options, transport);
    else
        engine = new asio_zmp_engine_t(fd_, options, transport);

    session->process_attach(engine);
}
```

### 8.10 클래스 의존성 다이어그램

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                          클래스 의존성 관계                                    │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│                            session_base_t                                    │
│                                  │                                           │
│       ┌──────────────┬───────────┼───────────┬──────────────┐               │
│       │              │           │           │              │               │
│       ▼              ▼           ▼           ▼              ▼               │
│  ┌──────────┐  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐         │
│  │asio_tcp_ │  │asio_tls_ │ │asio_ipc_ │ │asio_ws_  │ │(inproc)  │         │
│  │connecter │  │connecter │ │connecter │ │connecter │ │          │         │
│  └────┬─────┘  └────┬─────┘ └────┬─────┘ └────┬─────┘ └──────────┘         │
│       │             │            │            │                              │
│       │ creates     │ creates    │ creates    │ creates                      │
│       ▼             ▼            ▼            ▼                              │
│  ┌──────────┐  ┌──────────┐ ┌──────────┐ ┌───────────┐                      │
│  │tcp_      │  │ssl_      │ │ipc_      │ │ws_/wss_   │                      │
│  │transport │  │transport │ │transport │ │transport  │                      │
│  └────┬─────┘  └────┬─────┘ └────┬─────┘ └─────┬─────┘                      │
│       │             │            │             │                              │
│       └─────────────┴──────┬─────┴─────────────┘                             │
│                            │ implements                                       │
│                            ▼                                                  │
│                  ┌───────────────────┐                                       │
│                  │  i_asio_transport │                                       │
│                  │    (interface)    │                                       │
│                  └─────────┬─────────┘                                       │
│                            │ used by                                          │
│                            ▼                                                  │
│           ┌────────────────────────────────────┐                             │
│           │           asio_engine_t             │                             │
│           │            (base class)             │                             │
│           │   모든 트랜스포트(TCP/TLS/IPC/WS/WSS)에서 사용                    │
│           └────────────────┬───────────────────┘                             │
│                            │                                                  │
│                ┌───────────┴───────────┐                                     │
│                │                       │                                     │
│                ▼                       ▼                                     │
│       ┌─────────────────┐    ┌─────────────────┐                             │
│       │asio_zmp_engine_t│    │asio_raw_engine_t│                             │
│       │                 │    │                 │                             │
│       │ - ZMP 프로토콜   │    │ - RAW 프로토콜   │                             │
│       │ - HELLO/READY   │    │ - Length-Prefix │                             │
│       │ - 모든 트랜스포트│    │ - STREAM 소켓용 │                             │
│       └─────────────────┘    └─────────────────┘                             │
│                                                                              │
│  ※ WS/WSS도 asio_zmp_engine_t 또는 asio_raw_engine_t를 사용합니다.          │
│    WebSocket 프레이밍은 ws_transport_t/wss_transport_t가 처리합니다.          │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## 9. 소켓 패턴별 동작

### 9.1 PAIR 소켓

**특징**: 1:1 양방향 통신, 가장 단순한 패턴

```
┌─────────────┐                    ┌─────────────┐
│   PAIR A    │◄──────────────────►│   PAIR B    │
│             │    양방향 통신       │             │
│  send/recv  │                    │  send/recv  │
└─────────────┘                    └─────────────┘

- 단일 파이프만 유지
- 두 번째 연결 시도 시 거부
- 메시지 손실 없음 (1:1 보장)
```

### 9.2 DEALER/ROUTER 소켓

**DEALER**: 비동기 요청 전송, 라운드로빈 로드밸런싱

```
┌─────────────┐
│   DEALER    │
│             │
│  lb_t (Tx)  │────► Round-robin으로 여러 ROUTER에 분배
│  fq_t (Rx)  │◄──── 여러 ROUTER에서 공정하게 수신
└─────────────┘
```

**ROUTER**: ID 기반 라우팅, 다중 클라이언트 관리

```
┌─────────────────────────────────────────────────────────────┐
│                         ROUTER                              │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  수신 메시지 형식:                                          │
│  ┌──────────────┬─────────────────────────────────────┐   │
│  │ Routing ID   │           Payload                   │   │
│  │ (자동 추가)   │                                     │   │
│  └──────────────┴─────────────────────────────────────┘   │
│                                                             │
│  송신 시:                                                   │
│  - 첫 번째 프레임의 Routing ID로 대상 파이프 검색           │
│  - ID 제거 후 해당 파이프로 전송                            │
│                                                             │
│  ID 관리:                                                   │
│  - 자동 생성: uint32_t 증가값                               │
│  - 수동 설정: ZLINK_ROUTING_ID 옵션                           │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 9.3 PUB/SUB 소켓

**PUB**: 모든 구독자에게 메시지 브로드캐스트

```
┌─────────────┐
│     PUB     │
│             │
│   dist_t    │────► Fan-out: 모든 SUB에게 전송
│             │      (토픽 매칭은 SUB에서 수행)
└─────────────┘
```

**SUB**: 토픽 기반 필터링

```
┌─────────────────────────────────────────────────────────────┐
│                          SUB                                │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  구독 관리:                                                 │
│  zlink_setsockopt(socket, ZLINK_SUBSCRIBE, "topic", 5);        │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │                 radix_tree_t                         │   │
│  │                                                       │   │
│  │         (root)                                       │   │
│  │        /      \                                      │   │
│  │    "news"    "stock"                                 │   │
│  │     /          /   \                                 │   │
│  │  "weather"  "AAPL" "GOOGL"                          │   │
│  │                                                       │   │
│  │  O(m) 검색 (m = 토픽 길이)                           │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
│  메시지 필터링:                                             │
│  - 메시지 prefix가 구독 토픽과 일치하면 전달               │
│  - 빈 구독("")은 모든 메시지 수신                          │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 9.4 XPUB/XSUB 소켓

**XPUB**: 구독 메시지 수신 가능한 PUB

```
┌─────────────────────────────────────────────────────────────┐
│                         XPUB                                │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  일반 PUB 기능 + 구독 메시지 수신:                          │
│                                                             │
│  구독 메시지 형식:                                          │
│  ┌────────┬─────────────────────────────────────────┐     │
│  │ 0x01   │              Topic                      │     │
│  │(구독)  │                                          │     │
│  └────────┴─────────────────────────────────────────┘     │
│  ┌────────┬─────────────────────────────────────────┐     │
│  │ 0x00   │              Topic                      │     │
│  │(취소)  │                                          │     │
│  └────────┴─────────────────────────────────────────┘     │
│                                                             │
│  용도:                                                      │
│  - 프록시 구현 시 구독 정보 전달                            │
│  - 동적 구독 관리                                          │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**XSUB**: 구독 메시지 송신 가능한 SUB

```
- SUB와 동일한 필터링 기능
- 구독 요청을 메시지로 직접 전송 가능
- XPUB ↔ XSUB 프록시 체인 구성 가능

구독 자료구조:
- XPUB: mtrie_t (멀티 트라이)
- XSUB: ZLINK_USE_RADIX_TREE 매크로에 따라
  - radix_tree_t (활성화 시)
  - trie_with_size_t (기본)
```

### 9.5 STREAM 소켓

**특징**: 외부 클라이언트와의 RAW TCP 통신

```
┌─────────────────────────────────────────────────────────────┐
│                        STREAM                               │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Wire Format (네트워크):                                    │
│  ┌──────────────────┬─────────────────────────────────┐   │
│  │  Length (4B BE)  │        Payload (N bytes)        │   │
│  └──────────────────┴─────────────────────────────────┘   │
│                                                             │
│  Internal API (애플리케이션):                               │
│  ┌──────────────────┬─────────────────────────────────┐   │
│  │  Routing ID (4B) │        Payload (N bytes)        │   │
│  └──────────────────┴─────────────────────────────────┘   │
│                                                             │
│  변환:                                                      │
│  - Rx: Length 제거 → ID 조회 → [ID + Data] 전달            │
│  - Tx: ID로 파이프 검색 → ID 제거 → [Length + Data] 전송   │
│                                                             │
│  이벤트:                                                    │
│  - Connect:    [ID] + [0x01]                               │
│  - Disconnect: [ID] + [0x00]                               │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## 10. 성능 특성

### 10.1 벤치마크 결과 (Linux x64, 10회 측정)

**측정 조건**:
- 1회당 20,000회 전송
- 총 10회 반복 측정 (합계 200,000회)

**소켓 패턴별 처리량 (64B 메시지)**:

| 패턴 | TCP | IPC | inproc |
|------|-----|-----|--------|
| DEALER↔DEALER | 6.03 M/s | 5.96 M/s | 5.96 M/s |
| PAIR | 5.78 M/s | 5.65 M/s | **6.09 M/s** |
| DEALER↔ROUTER | 5.40 M/s | 5.55 M/s | 5.40 M/s |
| PUB/SUB | 5.76 M/s | 5.70 M/s | 5.71 M/s |
| ROUTER↔ROUTER | 5.03 M/s | 5.12 M/s | 4.71 M/s |
| ROUTER↔ROUTER_POLL | 4.83 M/s | 5.04 M/s | 3.99 M/s |

- **모든 트랜스포트에서 4-6 M/s** 처리량 달성 (64B 메시지)
- TCP/IPC/inproc 성능 차이 ±5% 이내 (64B 기준)
- 표준 libzlink 대비 ~99% 성능 동등성 달성

**지연 시간 (Latency)**:

| 트랜스포트 | 평균 지연 |
|-----------|----------|
| inproc | 0.07 ~ 0.5 μs |
| IPC | 15.6 ~ 37 μs |
| TCP | 16.7 ~ 42 μs |

### 10.2 WebSocket 성능 (STREAM 패턴)

| 메시지 크기 | TCP | WS | WSS |
|------------|-----|-----|-----|
| 64B | 5,518 K/s | 3,629 K/s | 3,566 K/s |
| 1KB | 1,315 K/s | 1,038 K/s | 497 K/s |
| 64KB | 63 K/s | 38 K/s | 15 K/s |

- WS는 TCP 대비 ~66% 처리량 (64B 기준)
- WSS는 TLS 오버헤드로 WS 대비 ~48% 처리량 (1KB 기준)

### 10.3 성능 최적화 기법

**1. Speculative I/O**
```
일반적인 비동기:
  zlink_send() → async_write() → 완료 대기 → 콜백

Speculative 최적화:
  zlink_send() → write_some() 시도
    ├─ 성공: 즉시 완료 (콜백 오버헤드 없음)
    └─ EAGAIN: async_write() 폴백
```

**2. Gather Write (Scatter-Gather I/O)**
```
일반:
  write(header, 8)   // 시스템 콜 1
  write(payload, N)  // 시스템 콜 2

Gather:
  writev([header, payload])  // 시스템 콜 1회로 통합
```

**3. Zero-Copy 메시지**
```cpp
// 대용량 데이터 전송 시 복사 방지
msg_t msg;
msg.init_data(buffer, size, my_free_fn, hint);  // 참조만 저장
zlink_msg_send(&msg, socket, 0);  // 복사 없이 전송
```

**4. VSM (Very Small Message)**
```
≤max_vsm_size (64-bit: 33 bytes): msg_t 내부 버퍼 사용 (malloc 없음)
>max_vsm_size: 별도 버퍼 할당
```

### 10.4 트랜스포트 선택 가이드

| 상황 | 권장 트랜스포트 | 이유 |
|------|----------------|------|
| 같은 머신, 최고 성능 | IPC | TCP 대비 +60% 처리량 |
| 같은 프로세스 | inproc | 메모리 공유, 최저 지연 |
| 네트워크 통신 | TCP | 표준적이고 안정적 |
| 암호화 필요 | TLS | 네이티브 SSL 지원 |
| 웹 클라이언트 | WS/WSS | 브라우저 호환 |

---

## 11. 소스 트리 구조

```
src/
├── api/                          # Public C API
│   ├── zlink.cpp                   # 모든 zlink_* 함수 진입점
│   └── zlink_utils.cpp             # 유틸리티 함수
│
├── core/                         # 시스템 기반 (71개 파일)
│   ├── ctx.cpp/hpp               # 컨텍스트 (스레드풀, 소켓 관리)
│   ├── msg.cpp/hpp               # 메시지 컨테이너 (64B 고정)
│   ├── pipe.cpp/hpp              # Lock-free 양방향 파이프
│   ├── session_base.cpp/hpp      # 소켓-엔진 브리지
│   ├── io_thread.cpp/hpp         # I/O 워커 스레드
│   ├── mailbox.cpp/hpp           # 스레드 간 명령 전달
│   ├── object.cpp/hpp            # 기반 객체 (명령 처리)
│   ├── own.cpp/hpp               # 소유 관계 관리
│   └── ...
│
├── engine/                       # I/O 엔진
│   └── asio/
│       ├── asio_engine.cpp/hpp   # 기반 Proactor 엔진
│       ├── asio_zmp_engine.cpp/hpp  # ZMP 프로토콜 엔진
│       ├── asio_raw_engine.cpp/hpp  # RAW 프로토콜 엔진
│       ├── asio_poller.cpp/hpp   # io_context 래퍼
│       ├── i_asio_transport.hpp  # 트랜스포트 인터페이스
│       └── handler_allocator.hpp # 핸들러 메모리 관리
│
├── protocol/                     # 프로토콜 인코딩/디코딩
│   ├── zmp_protocol.hpp          # ZMP v2.0 상수 정의
│   ├── zmp_encoder.cpp/hpp       # ZMP 인코더
│   ├── zmp_decoder.cpp/hpp       # ZMP 디코더
│   ├── raw_encoder.cpp/hpp       # RAW (Length-Prefix) 인코더
│   ├── raw_decoder.cpp/hpp       # RAW 디코더
│   ├── encoder.hpp               # 인코더 기반 템플릿
│   ├── decoder.hpp               # 디코더 기반 템플릿
│   └── metadata.cpp/hpp          # 메타데이터 처리
│
├── sockets/                      # 소켓 타입 구현 (23개 파일)
│   ├── socket_base.cpp/hpp       # 모든 소켓의 기반 클래스
│   ├── pair.cpp/hpp              # PAIR 소켓
│   ├── dealer.cpp/hpp            # DEALER 소켓
│   ├── router.cpp/hpp            # ROUTER 소켓
│   ├── pub.cpp/hpp               # PUB 소켓
│   ├── sub.cpp/hpp               # SUB 소켓
│   ├── xpub.cpp/hpp              # XPUB 소켓
│   ├── xsub.cpp/hpp              # XSUB 소켓
│   ├── stream.cpp/hpp            # STREAM 소켓
│   ├── lb.cpp/hpp                # 로드 밸런서
│   ├── fq.cpp/hpp                # 공정 큐
│   ├── dist.cpp/hpp              # 배포자 (Fan-out)
│   └── proxy.cpp/hpp             # 프록시 유틸리티
│
├── transports/                   # 트랜스포트 구현
│   ├── tcp/
│   │   ├── tcp_transport.cpp/hpp
│   │   ├── asio_tcp_connecter.cpp/hpp
│   │   ├── asio_tcp_listener.cpp/hpp
│   │   └── tcp_address.cpp/hpp
│   ├── ipc/                      # Unix only
│   │   ├── ipc_transport.cpp/hpp
│   │   ├── asio_ipc_connecter.cpp/hpp
│   │   ├── asio_ipc_listener.cpp/hpp
│   │   └── ipc_address.cpp/hpp
│   ├── ws/                       # WebSocket (Beast)
│   │   ├── ws_transport.cpp/hpp
│   │   ├── asio_ws_connecter.cpp/hpp
│   │   ├── asio_ws_listener.cpp/hpp
│   │   ├── asio_ws_engine.cpp/hpp  # (미사용, asio_zmp/raw_engine 사용)
│   │   └── ws_address.cpp/hpp
│   └── tls/                      # TLS/SSL (OpenSSL)
│       ├── ssl_transport.cpp/hpp
│       ├── wss_transport.cpp/hpp
│       ├── asio_tls_connecter.cpp/hpp
│       ├── asio_tls_listener.cpp/hpp
│       └── ssl_context_helper.cpp/hpp
│
└── utils/                        # 유틸리티 (80+ 파일)
    ├── ypipe.hpp                 # Lock-free 파이프
    ├── yqueue.hpp                # Lock-free 큐
    ├── atomic_counter.hpp        # 원자적 카운터
    ├── mutex.hpp                 # 뮤텍스 래퍼
    ├── clock.cpp/hpp             # 시간 측정
    ├── random.cpp/hpp            # 난수 생성
    ├── ip_resolver.cpp/hpp       # IP 주소 해석
    ├── mtrie.cpp/hpp             # 멀티 트라이 (구독)
    ├── trie.cpp/hpp              # 트라이
    ├── radix_tree.cpp/hpp        # 래딕스 트리
    └── ...
```

---

## 부록

### A. 관련 문서

- [ZMP v2 Protocol Specification](../rfc/ZMP_v2_SPEC.md)
- [STREAM Socket Architecture](./STREAM_SOCKET.md)
- [Performance Report](../report/performance/)

### B. 용어 사전

| 용어 | 설명 |
|------|------|
| **Proactor** | 비동기 I/O 완료 시 콜백을 호출하는 패턴 |
| **YPipe** | Lock-free FIFO 큐 구현 |
| **HWM** | High Water Mark, 큐 크기 제한 |
| **VSM** | Very Small Message, max_vsm_size 이하 메시지 최적화 (64-bit: 33 bytes) |
| **Speculative I/O** | 비동기 호출 전 동기 시도로 오버헤드 감소 |
| **Gather Write** | 여러 버퍼를 한 번의 시스템 콜로 전송 |

### C. 환경 변수

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `ZLINK_ASIO_IPC_SYNC_WRITE` | IPC speculative write 활성화 | OFF |
| `ZLINK_ASIO_IPC_FORCE_ASYNC` | IPC 강제 비동기 모드 | OFF |
| `ZLINK_ASIO_IPC_STATS` | IPC 통계 로깅 | OFF |

---

*이 문서는 zlink v0.5.0+ 기준으로 작성되었습니다.*
