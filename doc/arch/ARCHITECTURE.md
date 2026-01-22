# zlink (libzmq v0.5.0+) System Architecture

이 문서는 **zlink** 프로젝트의 핵심 설계 원칙과 시스템 구조를 설명합니다. zlink는 현대적인 Boost.Asio 비동기 모델과 TLS/WebSocket 환경에 최적화된 고성능 메시징 라이브러리입니다.

---

## 1. High-Level Architecture (ASCII View)

```text
================================================================================
                      zlink (libzmq v0.5.0) ARCHITECTURE
================================================================================

       +----------------------------------------------------------------+
       |                   Application Layer (User Code)                |
       +----------------------------------------------------------------+
                                     |
       +----------------------------------------------------------------+
       |                    Public API Layer (zmq.h)                    |
       |      [src/api] : PAIR, PUB/SUB, XPUB/XSUB, DEALER/ROUTER       |
       +----------------------------------------------------------------+
                                     | (Socket Logic)
       +-----------------------------V----------------------------------+
       |                      Socket Base Layer                         |
       |      [src/sockets] : Routing, Load Balancing, Policies         |
       +----------------------------------------------------------------+
                                     | (Internal msg_t)
       +-----------------------------V----------------------------------+
       |  [ EVENT ORCHESTRATOR ]     ASIO ENGINE                        |
       |  (The "WHEN")          +------------------------------------+  |
       |  [src/engine]          |  - io_context (Reactor/Proactor)   |  |
       |                        |  - Async Task Scheduling           |  |
       |                        +-----------------+------------------+  |
       +------------------------------------------|---------------------+
             ^ (Event Signals: "Ready!")          | (Trigger I/O Actions)
             |                                    |
       +-----|------------------------------------V---------------------+
       |  [ PROTOCOL LOGIC ]         ZMP LAYER                          |
       |  (The "WHAT")          +------------------------------------+  |
       |  [src/protocol]        |  zmp_encoder <---> zmp_decoder     |  |
       |                        |  (msg_t 64B <---> ZMP Byte Frames) |  |
       |                        +------------------------------------+  |
       +------------------------------------------|---------------------+
                                                  | (Serialized Data)
       +------------------------------------------V---------------------+
       |  [ PHYSICAL CARRIER ]       TRANSPORT LAYER                    |
       |  (The "HOW")           +------------------------------------+  |
       |  [src/transports]      |  tcp_t  |  ssl_t  |  ws_t  |  ipc_t  |  |
       |                        |  (Boost.Asio Socket / OpenSSL API) |  |
       +------------------------------------------|---------------------+
                                                  | (Physical Bytes)
       +------------------------------------------V---------------------+
       |  [ FOUNDATION ]             SYSTEM CORE                        |
       |  [src/core]            +------------------------------------+  |
       |                        |  msg_t, ctx_t, pipe_t, mailbox_t   |  |
       |                        +------------------------------------+  |
       +----------------------------------------------------------------+
```

---

## 2. Directory Structure & Responsibilities

소스 코드는 아키텍처 계층에 따라 물리적으로 분리되어 관리됩니다.

### 2.1 `src/api/` (Public Interface)
- **역할**: 외부 사용자가 호출하는 `zmq.h` 인터페이스의 구현체.
- **주요 파일**: `zmq.cpp`, `zmq_utils.cpp`.
- **설명**: 사용자의 요청을 받아 하위 소켓 레이어로 전달하는 얇은 래퍼(Wrapper) 계층입니다.

### 2.2 `src/sockets/` (Socket Business Logic)
- **역할**: 각 소켓 타입별 고유 로직(라우팅, 필터링 등) 및 공통 소켓 로직 처리.
- **주요 파일**: `socket_base.cpp`, `dealer.cpp`, `router.cpp`, `pub.cpp`, `sub.cpp`.
- **설명**: ZeroMQ 특유의 메시징 패턴이 구현되는 곳입니다.

### 2.3 `src/engine/` (Event Orchestration)
- **역할**: I/O 이벤트 루프 관리 및 비동기 핸들러 스케줄링.
- **주요 파일**: `asio_engine.cpp`, `asio_poller.cpp`.
- **설명**: Boost.Asio를 사용하여 시스템의 "언제(When)" 작업을 수행할지 결정하는 중앙 통제실입니다.

### 2.4 `src/protocol/` (Data Logic & Format)
- **역할**: ZMP(ZeroMQ Message Protocol) 인코딩/디코딩 및 프레이밍.
- **주요 파일**: `zmp_encoder.cpp`, `zmp_decoder.cpp`, `metadata.cpp`.
- **설명**: 데이터를 네트워크에 실어 보내기 위한 "무엇(What)"을 정의합니다.

### 2.5 `src/core/` (System Foundation)
- **역할**: 메시지 컨테이너, 동기화 큐, 컨텍스트 등 시스템의 기초 인프라.
- **주요 파일**: `msg.cpp`, `pipe.cpp`, `ctx.cpp`, `object.cpp`, `mailbox.cpp`.
- **설명**: **`msg_t`**는 프로토콜 데이터의 실체임과 동시에 시스템 전체의 메모리 관리 및 스레드 간 통신의 기초 골격이므로, 의존성 안전을 위해 최하위 기반 계층인 `core`에 위치합니다.

### 2.6 `src/transports/` (Physical Communication)
- **역할**: 각 프로토콜별 물리적 전송 수단 구현.
- **구조**: `tcp/`, `ipc/`, `ws/`, `tls/` 하위 디렉토리로 구성.
- **설명**: ASIO 엔진의 이벤트를 받아 실제로 바이트를 하위 매체로 쏘거나 읽어오는 "어떻게(How)"를 담당합니다.

### 2.7 `src/utils/` (Shared Utilities)
- **역할**: 전 계층에서 공통으로 사용하는 유틸리티.
- **내용**: 에러 처리(`err.cpp`), 시간(`clock.cpp`), 랜덤(`random.cpp`), 알고리즘(`trie.cpp`, `radix_tree.cpp`) 등.

---

## 3. Core Design Philosophy

- **Data-Centric Core**: `msg_t`를 64바이트 Flat 구조로 설계하여 하드웨어 성능(CPU Cache)을 최대한 활용합니다.
- **Event-Driven Execution**: 모든 통신은 Boost.Asio의 비동기 이벤트를 기반으로 동작하여 스레드 차단을 최소화합니다.
- **Technical Separation**: 이벤트 통제(Engine), 데이터 논리(Protocol), 물리 전송(Transport)을 엄격히 분리하여 확장성과 유지보수성을 극대화합니다.

---

## 4. Build & CI/CD
GitHub Actions를 통한 자동화된 멀티 플랫폼 빌드 시스템을 갖추고 있으며, 모든 바이너리는 SHA256 검증을 거쳐 배포됩니다.
