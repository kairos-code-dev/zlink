[English](architecture.md) | [한국어](architecture.ko.md)

# zlink System Architecture - Internal Developer Reference

This document describes the internal architecture of the **zlink** library in detail.
The intended audience is **internal developers** who develop or maintain the zlink library itself,
and it comprehensively covers the system's layer structure, core components, data flow, and source tree.

---

## Table of Contents

1. [Overview and Design Philosophy](#1-overview-and-design-philosophy)
2. [5-Layer Architecture](#2-5-layer-architecture)
3. [Component Relationships](#3-component-relationships)
4. [Socket Logic Layer Details](#4-socket-logic-layer-details)
5. [Engine Layer Details](#5-engine-layer-details)
6. [Core Components](#6-core-components)
7. [Data Flow](#7-data-flow)
8. [Source Tree Structure](#8-source-tree-structure)

---

## 1. Overview and Design Philosophy

### 1.1 What is zlink?

zlink is a high-performance messaging library based on libzmq.
While maintaining compatibility with libzmq's patterns and API, it applies the following modern design principles:

- **Boost.Asio-based I/O**: Uses Asio's unified asynchronous I/O instead of platform-specific pollers (epoll/kqueue/IOCP)
- **Native WebSocket/TLS support**: Built-in support for `ws://`, `wss://`, `tls://` protocols at the library level
- **Custom protocol stack**: Uses the lightweight **ZMP v2.0** protocol instead of ZMTP

### 1.2 Design Principles

```
┌──────────────────────┬──────────────────────────────────────────────────────┐
│       Principle       │                    Description                       │
├──────────────────────┼──────────────────────────────────────────────────────┤
│  Zero-Copy           │  Saves memory bandwidth by minimizing message copies │
│  Lock-Free           │  Uses lock-free data structures (YPipe) for ITC      │
│  True Async          │  True asynchronous I/O based on the Proactor pattern │
│  Protocol Agnostic   │  Clear separation of transport and protocol          │
└──────────────────────┴──────────────────────────────────────────────────────┘
```

### 1.3 Supported Sockets and Transports

**Socket Patterns (7 types)**

| Socket      | Type               | Description                        |
|-------------|--------------------|------------------------------------|
| PAIR        | 1:1 Bidirectional  | Single connection, bidirectional   |
| PUB / SUB   | Publish-Subscribe  | Topic-based broadcast              |
| XPUB / XSUB | Extended Pub-Sub  | Access to subscription messages    |
| DEALER      | Async Request      | Round-robin distribution           |
| ROUTER      | ID-based Routing   | Multi-client routing               |
| STREAM      | RAW TCP            | External client integration        |

**Transports (6 types)**

| Scheme     | Description                               |
|------------|-------------------------------------------|
| `tcp://`   | Standard TCP                              |
| `ipc://`   | Unix domain socket (Unix/Linux/macOS)     |
| `inproc://`| In-process communication (Lock-free pipe) |
| `ws://`    | WebSocket (Beast library)                 |
| `wss://`   | WebSocket over TLS                        |
| `tls://`   | Native TLS (OpenSSL)                      |

---

## 2. 5-Layer Architecture

zlink is composed of 5 clearly separated layers.
Each layer has a single responsibility, and layers closer to the bottom are closer to the physical network.

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          APPLICATION LAYER                              │
│                                                                         │
│   User code:                                                            │
│   zlink_ctx_new() -> zlink_socket() -> zlink_bind/connect()             │
│   -> zlink_send() / zlink_recv() -> zlink_close()                       │
│                                                                         │
├─────────────────────────────────────────────────────────────────────────┤
│                           PUBLIC API LAYER                              │
│                                                                         │
│   src/api/zlink.cpp                                                     │
│   - C API entry points (zlink_socket, zlink_send, zlink_recv, etc.)     │
│   - Error handling and parameter validation                             │
│                                                                         │
├─────────────────────────────────────────────────────────────────────────┤
│                          SOCKET LOGIC LAYER                             │
│                                                                         │
│   src/sockets/                                                          │
│   - socket_base_t: Base class for all sockets                           │
│   - pair_t, dealer_t, router_t, pub_t, sub_t, xpub_t, xsub_t, stream_t │
│   - Routing strategies: lb_t(RR), fq_t(Fair Queue), dist_t(Fan-out)    │
│   - Subscription management: mtrie_t(XPUB), radix_tree_t /             │
│     trie_with_size_t(XSUB)                                             │
│                                                                         │
├─────────────────────────────────────────────────────────────────────────┤
│                          ENGINE LAYER (ASIO)                            │
│                                                                         │
│   src/engine/asio/                                                      │
│   - asio_engine_t      : Proactor pattern-based async I/O engine (base) │
│   - asio_zmp_engine_t  : ZMP protocol (8B fixed header + handshake)     │
│   - asio_raw_engine_t  : RAW protocol (4B Length-Prefix, STREAM only)   │
│                                                                         │
├─────────────────────────────────────────────────────────────────────────┤
│                          PROTOCOL LAYER                                 │
│                                                                         │
│   ┌───────────────────────────┐    ┌───────────────────────────┐       │
│   │    ZMP v2.0 Protocol      │    │     RAW Protocol          │       │
│   │    src/protocol/zmp_*     │    │     src/protocol/raw_*    │       │
│   │    - 8-byte fixed header  │    │     - 4-byte length prefix│       │
│   │    - Handshake support    │    │     - No handshake        │       │
│   └───────────────────────────┘    └───────────────────────────┘       │
│                                                                         │
├─────────────────────────────────────────────────────────────────────────┤
│                          TRANSPORT LAYER                                │
│                                                                         │
│   src/transports/                                                       │
│   ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌──────────┐                 │
│   │   TCP   │  │   IPC   │  │   WS    │  │ TLS/WSS  │                 │
│   │  tcp_   │  │  ipc_   │  │  ws_    │  │  ssl_    │                 │
│   │transport│  │transport│  │transport│  │transport │                 │
│   └─────────┘  └─────────┘  └─────────┘  └──────────┘                 │
│                                                                         │
│   i_asio_transport: Unified async interface for all transports          │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

**Message passing path between layers**:
- Downward (Tx): Application -> API -> Socket Logic -> pipe_t -> Engine -> Protocol -> Transport
- Upward (Rx): Transport -> Protocol -> Engine -> pipe_t -> Socket Logic -> API -> Application

---

## 3. Component Relationships

The diagram below shows the ownership relationships and interactions between zlink internal objects.

```
┌──────────────────────────────────────────────────────────────────────┐
│                              ctx_t                                   │
│  (Global context: I/O thread pool, socket management, inproc        │
│   endpoints)                                                         │
└────────────────────────────────┬─────────────────────────────────────┘
                                 │ owns
            ┌────────────────────┼────────────────────┐
            │                    │                    │
            v                    v                    v
    ┌───────────────┐   ┌───────────────┐   ┌───────────────┐
    │  socket_base_t│   │  io_thread_t  │   │   reaper_t    │
    │ (socket inst.) │   │ (I/O worker)  │   │(resource      │
    │               │   │               │   │ cleanup)      │
    └───────┬───────┘   └───────┬───────┘   └───────────────┘
            │                   │
            │ owns              │ runs
            v                   v
    ┌───────────────┐   ┌───────────────┐
    │ session_base_t│   │  io_context   │
    │ (session mgmt)│   │ (Asio reactor)│
    └───────┬───────┘   └───────────────┘
            │
     ┌──────┴──────┐
     │             │
     v             v
┌─────────┐  ┌─────────────┐
│ pipe_t  │  │asio_engine_t│
│(msg que)│  │ (I/O engine) │
└─────────┘  └──────┬──────┘
                    │
                    v
            ┌───────────────┐
            │i_asio_transport│
            │  (transport)   │
            └───────────────┘
```

**Key ownership relationships**:

- `ctx_t` owns all `socket_base_t`, `io_thread_t`, and `reaper_t` instances.
- `socket_base_t` owns `session_base_t`, which acts as a bridge between the socket and the engine.
- `session_base_t` owns `pipe_t` (lock-free message queue) and `asio_engine_t` (I/O engine).
- `asio_engine_t` communicates with the physical transport layer through the `i_asio_transport` interface.
- `io_thread_t` holds an independent `io_context` for processing asynchronous I/O.
- `reaper_t` safely cleans up resources for terminated sockets/sessions.

---

## 4. Socket Logic Layer Details

### 4.1 Class Hierarchy

```
socket_base_t (base class)
├── pair_t              # PAIR socket: 1:1 bidirectional communication
├── dealer_t            # DEALER socket: async request, round-robin
├── router_t            # ROUTER socket: ID-based routing (inherits routing_socket_base_t)
├── xpub_t              # XPUB socket: can receive subscription messages
│   └── pub_t           # PUB socket: simplified XPUB (no subscription exposure)
├── xsub_t              # XSUB socket: can send subscription messages
│   └── sub_t           # SUB socket: simplified XSUB (subscribe via setsockopt)
└── stream_t            # STREAM socket: RAW TCP, external client integration
```

`socket_base_t` provides common functionality for all sockets:
- Connection management (`bind`, `connect`, `disconnect`, `unbind`)
- Pipe management (creation, termination, activation)
- Option management (`setsockopt`, `getsockopt`)
- Polling support (`has_in`, `has_out`)

### 4.2 Routing Strategy Classes

Strategy classes for message distribution and collection are separated by socket type:

```
┌──────────────────────────────────────────────────────────────────────┐
│                     Routing Strategies                                │
├──────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  lb_t (Load Balancer) - Sender-side round-robin              │    │
│  │                                                               │    │
│  │  Pipe A ──→ [ msg1 ]                                         │    │
│  │  Pipe B ──→ [ msg2 ]    ← Distributes in order               │    │
│  │  Pipe C ──→ [ msg3 ]                                         │    │
│  │                                                               │    │
│  │  Used by: DEALER (Tx)                                        │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  fq_t (Fair Queue) - Receiver-side fair queue                │    │
│  │                                                               │    │
│  │  Pipe A ←── [ msg ]                                          │    │
│  │  Pipe B ←── [ msg ]    ← Fairly receives from each pipe     │    │
│  │  Pipe C ←── [ msg ]                                          │    │
│  │                                                               │    │
│  │  Used by: DEALER (Rx), SUB (Rx)                              │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  dist_t (Distributor) - Broadcast fan-out                    │    │
│  │                                                               │    │
│  │  [ msg ] ──→ Pipe A                                          │    │
│  │          ──→ Pipe B    ← Sends the same message to all pipes │    │
│  │          ──→ Pipe C                                          │    │
│  │                                                               │    │
│  │  Used by: PUB, XPUB (Tx)                                    │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

### 4.3 Routing Strategy Mapping by Socket

| Socket  | Tx                    | Rx                   | Notes                            |
|---------|-----------------------|----------------------|----------------------------------|
| PAIR    | Single pipe           | Single pipe          | Only 1 pipe allowed              |
| DEALER  | `lb_t` (Round-robin)  | `fq_t` (Fair Queue)  | Async request-reply              |
| ROUTER  | ID-based direct route | `fq_t` (Fair Queue)  | Finds target pipe by Routing ID  |
| PUB     | `dist_t` (Fan-out)    | -                    | Cannot receive                   |
| SUB     | -                     | `fq_t` (Fair Queue)  | Topic filtering applied          |
| XPUB    | `dist_t` (Fan-out)    | Receives sub messages| Subscription managed by mtrie_t  |
| XSUB    | Sends sub messages    | `fq_t` (Fair Queue)  | Filtering + subscription sending |
| STREAM  | ID-based direct route | `fq_t` (Fair Queue)  | Uses RAW protocol                |

### 4.4 Subscription Data Structures

Trie-based data structures used for topic matching in PUB/SUB patterns:

```
┌─────────────────────────────────────────────────────────────┐
│                 Subscription Topic Trie Structure             │
│                                                              │
│                       (root)                                 │
│                      /      \                                │
│                  "news"    "stock"                            │
│                   /          /   \                            │
│              "weather"   "AAPL"  "GOOGL"                     │
│                                                              │
│  - XPUB: mtrie_t (multi-trie, per-pipe subscription tracking)│
│  - XSUB: Depends on ZLINK_USE_RADIX_TREE macro              │
│    - radix_tree_t (when enabled, memory-efficient)           │
│    - trie_with_size_t (default, fast lookup)                 │
│  - Lookup complexity: O(m), m = topic string length          │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

---

## 5. Engine Layer Details

### 5.1 Engine Type Comparison

The Engine Layer handles asynchronous I/O processing based on Boost.Asio.

| Engine                | Protocol  | Transports               | Features                           |
|-----------------------|-----------|--------------------------|-------------------------------------|
| `asio_zmp_engine_t`   | ZMP v2.0  | TCP, TLS, IPC, WS, WSS  | Handshake + 8-byte fixed header    |
| `asio_raw_engine_t`   | RAW       | TCP, TLS, IPC, WS, WSS  | 4-byte length prefix, STREAM only  |

> WS/WSS also use `asio_zmp_engine_t` or `asio_raw_engine_t`;
> WebSocket framing is handled by `ws_transport_t`/`wss_transport_t`.

### 5.2 Proactor Pattern Structure

```
┌──────────────────────────────────────────────────────────────────┐
│                        asio_engine_t                             │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────────────┐         ┌──────────────────────────────┐  │
│  │ async_read_some │--------→│      on_read_complete        │  │
│  │  (async read)   │         │  - Data receive complete CB   │  │
│  └─────────────────┘         │  - Parse message via decoder  │  │
│                              │  - Forward message to session │  │
│                              └──────────────────────────────┘  │
│                                                                  │
│  ┌─────────────────┐         ┌──────────────────────────────┐  │
│  │async_write_some │--------→│     on_write_complete        │  │
│  │  (async write)  │         │  - Data send complete CB      │  │
│  └─────────────────┘         │  - Encode next message        │  │
│                              │  - Repeat if more data to send│  │
│                              └──────────────────────────────┘  │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                  Speculative I/O (Optimization)          │   │
│  │                                                          │   │
│  │  speculative_read():                                     │   │
│  │    Attempts synchronous read first for immediately       │   │
│  │    available data                                        │   │
│  │    -> Improves throughput without async overhead          │   │
│  │                                                          │   │
│  │  speculative_write():                                    │   │
│  │    Writes synchronously if immediately possible          │   │
│  │    -> Completes instantly without callback on success    │   │
│  │    -> Falls back to async_write_some() on EAGAIN         │   │
│  │                                                          │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                   Backpressure                           │   │
│  │                                                          │   │
│  │  _pending_buffers: Temporary storage for unprocessed data│   │
│  │  max_pending_buffer_size: 10MB limit                     │   │
│  │  Pauses reading when limit exceeded -> resumes when      │   │
│  │  space becomes available                                 │   │
│  │                                                          │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

### 5.3 Engine State Machine

```
          ┌─────────────────────┐
          │      Created        │
          └──────────┬──────────┘
                     │ plug()
                     v
          ┌─────────────────────┐
          │    Handshaking      │  TLS/WebSocket: transport handshake
          │  (_handshaking)     │  ZMP: protocol handshake
          └──────────┬──────────┘
                     │ handshake complete
                     v
          ┌─────────────────────┐
          │      Active         │ <-----------------┐
          │   Data send/recv    │                   │
          └──────────┬──────────┘                   │
                     │ I/O error                     │ restart
                     v                              │
          ┌─────────────────────┐                   │
          │       Error         │ -----------------┘
          └──────────┬──────────┘
                     │ terminate()
                     v
          ┌─────────────────────┐
          │    Terminated       │
          └─────────────────────┘
```

### 5.4 ZMP v2.0 Frame Structure

```
 Byte:   0         1         2         3         4    5    6    7
      ┌─────────┬─────────┬─────────┬─────────┬─────────────────────┐
      │  MAGIC  │ VERSION │  FLAGS  │RESERVED │   PAYLOAD SIZE      │
      │  (0x5A) │  (0x02) │         │ (0x00)  │   (32-bit BE)       │
      └─────────┴─────────┴─────────┴─────────┴─────────────────────┘
```

| Field        | Offset | Size | Description               |
|-------------|--------|------|---------------------------|
| MAGIC       | 0      | 1    | Magic number `0x5A` ('Z') |
| VERSION     | 1      | 1    | Protocol version `0x02`   |
| FLAGS       | 2      | 1    | Frame flags               |
| RESERVED    | 3      | 1    | Reserved (0x00)           |
| PAYLOAD SIZE| 4-7    | 4    | Payload size (Big Endian) |

**FLAGS bit definitions**:

| Bit  | Name      | Description            |
|------|-----------|------------------------|
| 0    | MORE      | Multipart message cont.|
| 1    | CONTROL   | Control frame          |
| 2    | IDENTITY  | Contains Routing ID    |
| 3    | SUBSCRIBE | Subscription request   |
| 4    | CANCEL    | Subscription cancel    |

### 5.5 RAW Protocol Frame Structure

A simple protocol for STREAM sockets and external client integration.

```
┌──────────────────────┬─────────────────────────────┐
│  Length (4 Bytes)    │     Payload (N Bytes)       │
│  (Big Endian)        │                             │
└──────────────────────┴─────────────────────────────┘
```

- No handshake (immediate data send/receive)
- Simple implementation: `read(4)` -> `read(length)`
- Easy integration with external clients

### 5.6 ZMP Handshake Sequence

```
    Client                              Server
       │                                   │
       │─────── HELLO (greeting) ─────────→│
       │                                   │
       │←────── HELLO (greeting) ──────────│
       │                                   │
       │                                   │  (Socket type compatibility check)
       │                                   │
       │─────── READY (metadata) ─────────→│
       │                                   │
       │←────── READY (metadata) ──────────│
       │                                   │
       │←─────── Data Exchange ───────────→│
       │                                   │
```

- **HELLO**: Socket type (1B) + Identity length (1B) + Identity value (0-255B)
- **READY**: Socket-Type property (always), Identity property (DEALER/ROUTER only)

### 5.7 Protocol-Transport-Engine Mapping

The engine is automatically selected based on the socket type:

```
┌─────────────────────────────────────────────────────────────────────┐
│                       Engine Selection Rules                         │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  Socket type == STREAM ?                                            │
│      ├─ YES → asio_raw_engine_t  (RAW protocol, no handshake)      │
│      └─ NO  → asio_zmp_engine_t  (ZMP protocol, HELLO/READY)      │
│                                                                     │
│  This rule is the same across all transports (TCP/TLS/IPC/WS/WSS). │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

**Full Mapping Matrix**:

| URL Scheme | Connecter               | Transport          | STREAM Engine       | Other Socket Engine  | Handshake           |
|-----------|-------------------------|--------------------|---------------------|---------------------|---------------------|
| `tcp://`  | `asio_tcp_connecter_t`  | `tcp_transport_t`  | `asio_raw_engine_t` | `asio_zmp_engine_t` | (none) / ZMP        |
| `tls://`  | `asio_tls_connecter_t`  | `ssl_transport_t`  | `asio_raw_engine_t` | `asio_zmp_engine_t` | SSL / SSL+ZMP       |
| `ws://`   | `asio_ws_connecter_t`   | `ws_transport_t`   | `asio_raw_engine_t` | `asio_zmp_engine_t` | WS / WS+ZMP        |
| `wss://`  | `asio_ws_connecter_t`   | `wss_transport_t`  | `asio_raw_engine_t` | `asio_zmp_engine_t` | SSL+WS / SSL+WS+ZMP|
| `ipc://`  | `asio_ipc_connecter_t`  | `ipc_transport_t`  | `asio_raw_engine_t` | `asio_zmp_engine_t` | (none) / ZMP        |

### 5.8 Handshake Stage Comparison

```
┌─────────────────────────────────────────────────────────────────────────┐
│                      Handshake Stage Comparison                         │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  TCP + PAIR/DEALER/ROUTER/PUB/SUB                                      │
│  ┌─────────┐    ┌─────────────┐                                       │
│  │  TCP    │───→│  ZMP        │───→ Data Transfer                     │
│  │ Connect │    │  Handshake  │                                       │
│  └─────────┘    └─────────────┘                                       │
│                                                                         │
│  TCP + STREAM                                                          │
│  ┌─────────┐                                                           │
│  │  TCP    │───────────────────────→ Data Transfer (immediate)         │
│  │ Connect │                                                           │
│  └─────────┘                                                           │
│                                                                         │
│  TLS + PAIR/DEALER/ROUTER/PUB/SUB                                      │
│  ┌─────────┐    ┌─────────┐    ┌─────────────┐                        │
│  │  TCP    │───→│  SSL    │───→│  ZMP        │───→ Data Transfer      │
│  │ Connect │    │Handshake│    │  Handshake  │                        │
│  └─────────┘    └─────────┘    └─────────────┘                        │
│                                                                         │
│  WS + PAIR/DEALER/ROUTER/PUB/SUB                                       │
│  ┌─────────┐    ┌─────────┐    ┌─────────────┐                        │
│  │  TCP    │───→│   WS    │───→│  ZMP        │───→ Data Transfer      │
│  │ Connect │    │ Upgrade │    │  Handshake  │                        │
│  └─────────┘    └─────────┘    └─────────────┘                        │
│                                                                         │
│  WSS + PAIR/DEALER/ROUTER/PUB/SUB                                      │
│  ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────────┐        │
│  │  TCP    │───→│  SSL    │───→│   WS    │───→│  ZMP        │───→ Tx │
│  │ Connect │    │Handshake│    │ Upgrade │    │  Handshake  │        │
│  └─────────┘    └─────────┘    └─────────┘    └─────────────┘        │
│                                                                         │
│  WSS + STREAM                                                          │
│  ┌─────────┐    ┌─────────┐    ┌─────────┐                            │
│  │  TCP    │───→│  SSL    │───→│   WS    │───────────────→ Data Tx    │
│  │ Connect │    │Handshake│    │ Upgrade │                            │
│  └─────────┘    └─────────┘    └─────────┘                            │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5.9 Transport Characteristics Comparison

| Transport | Handshake  | Encryption | Speculative Write | Gather Write | Use Case                      |
|-----------|:----------:|:----------:|:-----------------:|:------------:|-------------------------------|
| TCP       | -          | -          | O                 | O            | Standard network communication|
| IPC       | -          | -          | Optional          | O            | Local inter-process comms     |
| TLS       | O          | O          | -                 | -            | Encrypted network communication|
| WS        | O          | -          | -                 | O            | Web client integration        |
| WSS       | O          | O          | -                 | O            | Encrypted web client          |

---

## 6. Core Components

### 6.1 msg_t - Message Container

A 64-byte fixed-size structure that holds all message data.
It is designed to handle small messages without `malloc` calls.

```
┌─────────────────────────────────────────────────────────────────┐
│                        msg_t (64 bytes)                         │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │  Common fields (base_t)                                    │ │
│  │  - metadata_t* metadata   (8 bytes)                        │ │
│  │  - uint32_t routing_id    (4 bytes)                        │ │
│  │  - group_t group          (16 bytes)                       │ │
│  │  - uint8_t flags          (1 byte)                         │ │
│  │  - uint8_t type           (1 byte)                         │ │
│  └───────────────────────────────────────────────────────────┘ │
│                                                                 │
│  Type-specific data area (union):                               │
│                                                                 │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │  type_vsm (<=33B on 64-bit)                                │ │
│  │  Very Small Message: data stored directly in msg_t buffer  │ │
│  │  - uint8_t data[max_vsm_size]                              │ │
│  │  - uint8_t size                                            │ │
│  │  -> Inline storage without malloc, fastest path            │ │
│  └───────────────────────────────────────────────────────────┘ │
│                            OR                                   │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │  type_lmsg (>33B on 64-bit)                                │ │
│  │  Large Message: pointer to separately allocated buffer     │ │
│  │  - content_t* content                                      │ │
│  │    ├── void* data          (data pointer)                  │ │
│  │    ├── size_t size         (size)                           │ │
│  │    ├── msg_free_fn* ffn    (free function)                 │ │
│  │    └── atomic_counter_t refcnt (reference count)           │ │
│  └───────────────────────────────────────────────────────────┘ │
│                            OR                                   │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │  type_cmsg: Constant Message (const data ref, no free)     │ │
│  │  type_zclmsg: Zero-copy Large Message (direct user buffer) │ │
│  └───────────────────────────────────────────────────────────┘ │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**Message flags**:

| Flag         | Value | Description                            |
|-------------|-------|----------------------------------------|
| `more`       | 0x01 | Intermediate frame of multipart message |
| `command`    | 0x02 | Control frame (handshake, heartbeat)   |
| `routing_id` | 0x40 | Contains Routing ID                    |
| `shared`     | 0x80 | Shared buffer (reference counted)      |

**Message types**:

| Type          | Value | Description                                     |
|--------------|-------|-------------------------------------------------|
| `type_vsm`    | 101  | Very Small Message (<=33B, no copy)             |
| `type_lmsg`   | 102  | Large Message (malloc'd buffer)                 |
| `type_cmsg`   | 104  | Constant Message (const data reference)         |
| `type_zclmsg` | 105  | Zero-copy Large Message (direct user buffer)    |

### 6.2 pipe_t - Lock-Free Message Queue

A bidirectional pipe for passing messages between threads.
It exchanges `msg_t` instances lock-free between the Application thread and the I/O thread.

```
┌───────────────────────────────────────────────────────────────┐
│                          pipe_t                               │
├───────────────────────────────────────────────────────────────┤
│                                                               │
│  Thread A (Socket)              Thread B (I/O)                │
│       │                              │                        │
│       │    ┌──────────────────┐     │                        │
│       ├───→│   _out_pipe      │────→│  (Tx: Socket -> I/O)   │
│       │    │   (YPipe<msg_t>) │     │                        │
│       │    └──────────────────┘     │                        │
│       │                              │                        │
│       │    ┌──────────────────┐     │                        │
│       │←───│   _in_pipe       │←────┤  (Rx: I/O -> Socket)   │
│       │    │   (YPipe<msg_t>) │     │                        │
│       │    └──────────────────┘     │                        │
│                                                               │
│  High Water Mark (HWM): Message queue size limit              │
│  - _hwm: Outbound HWM (blocks send when queue exceeded)      │
│  - _lwm: Inbound Low Water Mark (half of HWM, resume point)  │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

**YPipe characteristics**:
- Lock-free FIFO queue (CAS operation-based)
- Cache line optimization
- Visibility guaranteed through memory barriers

**Pipe state machine**:

```
                    ┌────────────┐
                    │   active   │ <────────────────┐
                    └─────┬──────┘                  │
                          │ receive delimiter       │ connect
                          v                         │
              ┌───────────────────────┐             │
              │ delimiter_received    │             │
              └───────────┬───────────┘             │
                          │ send term_ack           │
                          v                         │
              ┌───────────────────────┐             │
              │    term_ack_sent      │             │
              └───────────┬───────────┘             │
                          │ receive term_ack        │
                          v                         │
                    ┌───────────┐                   │
                    │ terminated│ ──────────────────┘
                    └───────────┘     (on reconnect)
```

### 6.3 ctx_t - Context

The top-level object that manages global state.

**Key roles**:

1. **I/O Thread Pool Management**
   - Set thread count with `zlink_ctx_set(ctx, ZLINK_IO_THREADS, n)` (default: 1)
   - Each I/O thread holds an independent `io_context`
   - Selects the least-loaded I/O thread for new connections (affinity mask support)

2. **Socket Management**
   - Tracks socket creation/deletion
   - Maximum socket limit (default: 1023)
   - Empty slot reuse

3. **inproc Endpoint Management**
   - Maps `inproc://name` format addresses to endpoints
   - Holds connection requests made before bind in pending_connections

```
ctx_t internal structure:
┌──────────────────────────────────────────────────────────┐
│  _sockets: array_t<socket_base_t>     Active socket list  │
│  _empty_slots: vector<uint32_t>       Empty slot reuse    │
│  _io_threads: vector<io_thread_t*>    I/O thread pool     │
│  _slots: vector<i_mailbox*>           Inter-thread mailbox│
│  _endpoints: map<string, endpoint_t>  inproc registry     │
│  _pending_connections: multimap       Pending connections  │
│                                                          │
│  _max_sockets: int     (default: 1023)                   │
│  _io_thread_count: int (default: 1)                      │
│  _max_msgsz: int       (max message size)                │
└──────────────────────────────────────────────────────────┘
```

### 6.4 session_base_t - Session

Acts as a bridge between the socket and the engine.

```
┌─────────────────────────────────────────────────────────────┐
│                     session_base_t                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────┐    ┌─────────┐    ┌─────────────────┐   │
│  │ socket_base_t│←──→│ pipe_t  │←──→│ asio_engine_t   │   │
│  │              │    │         │    │                 │   │
│  │  zlink_send() │    │ YPipe   │    │ async_read/     │   │
│  │  zlink_recv() │    │         │    │ async_write     │   │
│  └──────────────┘    └─────────┘    └─────────────────┘   │
│                                                             │
│  push_msg(): Engine -> Session -> Pipe -> Socket            │
│  pull_msg(): Socket -> Pipe -> Session -> Engine            │
│                                                             │
│  Additional roles:                                          │
│  - Connection state management                              │
│  - Reconnection logic (exponential backoff)                 │
│  - Connecter selection (based on URL scheme)                │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 6.5 Threading Model

```
┌─────────────────────────────────────────────────────────────────┐
│                    zlink Threading Model                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                 Application Threads                      │   │
│  │  - Call zlink_send() / zlink_recv()                     │   │
│  │  - Recommended: access each socket from a single thread │   │
│  │  - Multiple sockets can be used from multiple threads   │   │
│  └──────────────────────────┬──────────────────────────────┘   │
│                              │                                  │
│                   Lock-free Pipes (YPipe)                       │
│                              │                                  │
│  ┌──────────────────────────v──────────────────────────────┐   │
│  │                    I/O Threads                           │   │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐                │   │
│  │  │ Thread 0 │ │ Thread 1 │ │ Thread N │  (configurable) │   │
│  │  │io_context│ │io_context│ │io_context│                │   │
│  │  └──────────┘ └──────────┘ └──────────┘                │   │
│  │                                                          │   │
│  │  - Asynchronous I/O processing (Proactor pattern)       │   │
│  │  - Encoder/decoder execution                             │   │
│  │  - Network send/receive                                  │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    Reaper Thread                         │   │
│  │  - Resource cleanup for terminated sockets/sessions     │   │
│  │  - Deferred deletion processing                          │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**Inter-thread communication (Mailbox system)**:

```
Application Thread              I/O Thread
      │                              │
      │  zlink_send()                 │
      │      │                       │
      │      v                       │
      │  [Push msg_t to YPipe]       │
      │      │                       │
      │  mailbox.send(activate_write)│
      │─────────────────────────────→│
      │                              │  (signal received)
      │                              │
      │                              v
      │                         [Pop msg_t from YPipe]
      │                              │
      │                         [Encode and transmit]
```

- Each thread has its own `mailbox_t`.
- `mailbox_t` internally consists of `ypipe_t<command_t>` and `signaler_t`.
- Command types: `stop`, `plug`, `attach`, `bind`, `activate_read`, `activate_write`, etc.

---

## 7. Data Flow

### 7.1 Message Send (Outbound / Tx)

```
┌───────────────────────────────────────────────────────────────────┐
│                    APPLICATION THREAD                             │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  (1) zlink_send(socket, data, size, flags)                        │
│       │                                                           │
│       v                                                           │
│  (2) socket_base_t::send()                                       │
│       │  - Create msg_t (VSM or LMSG)                            │
│       │  - Select routing strategy by socket type                 │
│       │    . DEALER: lb_t (Round-robin)                          │
│       │    . ROUTER: ID-based direct routing                     │
│       │    . PUB: dist_t (send to all subscribers)               │
│       v                                                           │
│  (3) pipe_t::write()                                             │
│       │  - Push message to YPipe (Lock-free)                     │
│       │  - HWM check (block or drop when exceeded)               │
│       v                                                           │
│  (4) mailbox signal to I/O thread                                │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
                              │
                              v
┌───────────────────────────────────────────────────────────────────┐
│                      I/O THREAD                                   │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  (5) asio_engine_t: receive activate_write event                  │
│       │                                                           │
│       v                                                           │
│  (6) pull_msg_from_session()                                     │
│       │  - Read message from pipe                                │
│       v                                                           │
│  (7) encoder: message -> byte stream                              │
│       │  - ZMP: 8-byte header + payload                          │
│       │  - RAW: 4-byte length + payload                          │
│       v                                                           │
│  (8) speculative_write() attempt                                 │
│       │  - Success: synchronous write completes immediately       │
│       │  - Failure (EAGAIN): schedule async_write_some()         │
│       v                                                           │
│  (9) transport: network transmission                              │
│       - TCP: direct send                                          │
│       - TLS: encrypt with SSL then send                          │
│       - WS: Beast WebSocket framing then send                    │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

### 7.2 Message Receive (Inbound / Rx)

```
┌───────────────────────────────────────────────────────────────────┐
│                      I/O THREAD                                   │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  (1) async_read_some() completion callback                        │
│       │  - Receive bytes from network                            │
│       v                                                           │
│  (2) on_read_complete()                                          │
│       │                                                           │
│       v                                                           │
│  (3) decoder: byte stream -> message                              │
│       │  - Parse header (ZMP 8B / RAW 4B)                        │
│       │  - Verify payload size                                   │
│       │  - Create msg_t                                          │
│       v                                                           │
│  (4) push_msg_to_session()                                       │
│       │                                                           │
│       v                                                           │
│  (5) session_base_t::push_msg()                                  │
│       │  - Message validation                                    │
│       │  - Forward to inbound pipe                               │
│       v                                                           │
│  (6) pipe_t::write() (inbound pipe)                              │
│       │  - Push message to YPipe                                 │
│       v                                                           │
│  (7) Signal read-ready to socket (activate_read)                 │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
                              │
                              v
┌───────────────────────────────────────────────────────────────────┐
│                    APPLICATION THREAD                             │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  (8) zlink_recv(socket, buffer, size, flags)                      │
│       │                                                           │
│       v                                                           │
│  (9) socket_base_t::recv()                                       │
│       │  - Receive strategy by socket type                       │
│       │    . DEALER/SUB: fq_t (Fair Queueing)                   │
│       │    . ROUTER: extract Routing ID then deliver message     │
│       │  - Topic filtering (SUB)                                 │
│       v                                                           │
│  (10) pipe_t::read()                                             │
│        │  - Pop message from YPipe (Lock-free)                   │
│        v                                                          │
│  (11) Copy data to user buffer                                   │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

### 7.3 Connection Establishment Flow

```
┌───────────────────────────────────────────────────────────────────┐
│                   Connection Establishment Steps                   │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  zlink_connect("tcp://host:port")                                 │
│       │                                                           │
│       v                                                           │
│  (1) address_t parsing                                            │
│       │  - Identify protocol (tcp, tls, ws, wss, ipc)            │
│       │  - Extract address/port                                   │
│       v                                                           │
│  (2) Create session_base_t                                       │
│       │  - Set reconnection policy                                │
│       v                                                           │
│  (3) Create and start connecter                                   │
│       │  - Select connecter class based on URL scheme             │
│       │  - Call async_connect()                                   │
│       v                                                           │
│  (4) TCP connection complete (3-way handshake)                    │
│       │                                                           │
│       v                                                           │
│  (5) [TLS/WSS] Transport handshake                                │
│       │  - TLS: SSL_do_handshake()                               │
│       │  - WS: HTTP Upgrade request/response                     │
│       v                                                           │
│  (6) Create engine and plug()                                    │
│       │  - Select asio_zmp_engine_t or asio_raw_engine_t         │
│       │    based on socket type                                   │
│       v                                                           │
│  (7) [ZMP] Protocol handshake                                     │
│       │  - HELLO exchange (socket type, Identity)                │
│       │  - Socket type compatibility check                       │
│       │  - READY exchange (metadata)                             │
│       v                                                           │
│  (8) engine_ready()                                              │
│       - Create and connect pipe                                   │
│       - start_input() / start_output()                            │
│       -> Data send/receive is now possible                        │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

---

## 8. Source Tree Structure

```
core/
├── include/                         # Public headers (zlink.h)
│
├── src/
│   ├── api/                         # Public C API
│   │   ├── zlink.cpp                # Entry point for all zlink_* functions
│   │   └── zlink_utils.cpp          # Utility functions
│   │
│   ├── core/                        # System base components
│   │   ├── ctx.cpp/hpp              # Context (thread pool, socket management)
│   │   ├── msg.cpp/hpp              # Message container (64B fixed)
│   │   ├── pipe.cpp/hpp             # Lock-free bidirectional pipe
│   │   ├── session_base.cpp/hpp     # Socket-engine bridge
│   │   ├── io_thread.cpp/hpp        # I/O worker thread
│   │   ├── mailbox.cpp/hpp          # Inter-thread command delivery
│   │   ├── object.cpp/hpp           # Base object (command processing)
│   │   ├── own.cpp/hpp              # Ownership management
│   │   ├── reaper.cpp/hpp           # Terminated resource cleanup
│   │   ├── signaler.cpp/hpp         # Thread wake-up signal
│   │   ├── options.cpp/hpp          # Socket option storage
│   │   ├── address.cpp/hpp          # Address parsing
│   │   ├── endpoint.cpp/hpp         # Endpoint management
│   │   ├── command.hpp              # Inter-thread command definitions
│   │   ├── socket_poller.cpp/hpp    # Socket poller
│   │   └── ...
│   │
│   ├── sockets/                     # Socket type implementations
│   │   ├── socket_base.cpp/hpp      # Base class for all sockets
│   │   ├── pair.cpp/hpp             # PAIR socket
│   │   ├── dealer.cpp/hpp           # DEALER socket
│   │   ├── router.cpp/hpp           # ROUTER socket
│   │   ├── pub.cpp/hpp              # PUB socket
│   │   ├── sub.cpp/hpp              # SUB socket
│   │   ├── xpub.cpp/hpp             # XPUB socket
│   │   ├── xsub.cpp/hpp             # XSUB socket
│   │   ├── stream.cpp/hpp           # STREAM socket
│   │   ├── lb.cpp/hpp               # Load balancer (Round-robin)
│   │   ├── fq.cpp/hpp               # Fair queue (Fair Queueing)
│   │   ├── dist.cpp/hpp             # Distributor (Fan-out)
│   │   └── proxy.cpp/hpp            # Proxy utility
│   │
│   ├── engine/                      # I/O engines
│   │   ├── i_engine.hpp             # Engine interface
│   │   └── asio/
│   │       ├── asio_engine.cpp/hpp       # Base Proactor engine
│   │       ├── asio_zmp_engine.cpp/hpp   # ZMP protocol engine
│   │       ├── asio_raw_engine.cpp/hpp   # RAW protocol engine
│   │       ├── asio_poller.cpp/hpp       # io_context wrapper
│   │       ├── i_asio_transport.hpp      # Transport interface
│   │       ├── handler_allocator.hpp     # Handler memory management
│   │       └── asio_error_handler.hpp    # Error handling
│   │
│   ├── protocol/                    # Protocol encoding/decoding
│   │   ├── zmp_protocol.hpp         # ZMP v2.0 constant definitions
│   │   ├── zmp_encoder.cpp/hpp      # ZMP encoder
│   │   ├── zmp_decoder.cpp/hpp      # ZMP decoder
│   │   ├── zmp_metadata.hpp         # ZMP metadata
│   │   ├── raw_encoder.cpp/hpp      # RAW (Length-Prefix) encoder
│   │   ├── raw_decoder.cpp/hpp      # RAW decoder
│   │   ├── encoder.hpp              # Encoder base template
│   │   ├── decoder.hpp              # Decoder base template
│   │   ├── i_encoder.hpp            # Encoder interface
│   │   ├── i_decoder.hpp            # Decoder interface
│   │   ├── metadata.cpp/hpp         # Metadata processing
│   │   ├── wire.hpp                 # Byte order conversion
│   │   └── decoder_allocators.cpp/hpp # Decoder memory management
│   │
│   ├── transports/                  # Transport implementations
│   │   ├── tcp/                     # TCP transport
│   │   │   ├── tcp_transport.cpp/hpp
│   │   │   ├── asio_tcp_connecter.cpp/hpp
│   │   │   ├── asio_tcp_listener.cpp/hpp
│   │   │   ├── tcp_address.cpp/hpp
│   │   │   └── tcp.cpp/hpp
│   │   │
│   │   ├── ipc/                     # IPC transport (Unix only)
│   │   │   ├── ipc_transport.cpp/hpp
│   │   │   ├── asio_ipc_connecter.cpp/hpp
│   │   │   ├── asio_ipc_listener.cpp/hpp
│   │   │   └── ipc_address.cpp/hpp
│   │   │
│   │   ├── ws/                      # WebSocket transport (Beast)
│   │   │   ├── ws_transport.cpp/hpp
│   │   │   ├── asio_ws_connecter.cpp/hpp
│   │   │   ├── asio_ws_listener.cpp/hpp
│   │   │   ├── asio_ws_engine.cpp/hpp   # (unused, uses asio_zmp/raw_engine)
│   │   │   └── ws_address.cpp/hpp
│   │   │
│   │   └── tls/                     # TLS/SSL transport (OpenSSL)
│   │       ├── ssl_transport.cpp/hpp
│   │       ├── wss_transport.cpp/hpp
│   │       ├── asio_tls_connecter.cpp/hpp
│   │       ├── asio_tls_listener.cpp/hpp
│   │       ├── ssl_context_helper.cpp/hpp
│   │       └── wss_address.cpp/hpp
│   │
│   ├── services/                    # High-level services
│   │   ├── common/                  # Common service utilities
│   │   │   ├── heartbeat.hpp
│   │   │   ├── service_key.hpp
│   │   │   └── service_types.hpp
│   │   ├── discovery/               # Service discovery
│   │   │   ├── discovery.cpp/hpp
│   │   │   ├── discovery_protocol.hpp
│   │   │   └── registry.cpp/hpp
│   │   ├── gateway/                 # Gateway
│   │   │   ├── gateway.cpp/hpp
│   │   │   ├── receiver.cpp/hpp
│   │   │   └── routing_id_utils.hpp
│   │   └── spot/                    # SPOT service
│   │       ├── spot_pub.cpp/hpp     # Publish handle (thread-safe)
│   │       ├── spot_sub.cpp/hpp     # Subscribe/receive handle
│   │       └── spot_node.cpp/hpp    # Network control (PUB/SUB mesh)
│   │
│   └── utils/                       # Utilities
│       ├── ypipe.hpp                # Lock-free pipe
│       ├── yqueue.hpp               # Lock-free queue
│       ├── atomic_counter.hpp       # Atomic counter
│       ├── atomic_ptr.hpp           # Atomic pointer
│       ├── blob.hpp                 # Binary blob
│       ├── clock.cpp/hpp            # Time measurement
│       ├── random.cpp/hpp           # Random number generation
│       ├── ip_resolver.cpp/hpp      # IP address resolution
│       ├── mtrie.cpp/hpp            # Multi-trie (XPUB subscriptions)
│       ├── trie.cpp/hpp             # Trie
│       ├── radix_tree.cpp/hpp       # Radix tree (XSUB subscriptions)
│       ├── generic_mtrie.hpp        # Generic multi-trie template
│       ├── mutex.hpp                # Mutex wrapper
│       ├── condition_variable.hpp   # Condition variable wrapper
│       ├── err.cpp/hpp              # Error handling
│       ├── ip.cpp/hpp               # IP utilities
│       ├── config.hpp               # Compile-time configuration
│       └── ...
│
├── tests/                           # Functional tests
└── unittests/                       # Internal unit tests
```

---

## Appendix

### A. Related Documents

- [ZMP v2.0 Protocol Details](protocol-zmp.md)
- [RAW Protocol Details](protocol-raw.md)
- [STREAM Socket WS/WSS Optimization](stream-socket.md)
- [Threading and Concurrency Model](threading-model.md)
- [Performance Characteristics and Tuning Guide](../guide/10-performance.md)

### B. Core Interface Summary

**i_asio_transport** (common interface for all transports):

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

**i_engine** (engine interface):

```
i_engine
  +-- plug(session)                     Connect to session
  +-- terminate()                       Terminate
  +-- restart_input()                   Restart receive
  +-- restart_output()                  Restart send
```

### C. Performance Optimization Techniques Summary

| Technique           | Description                                                         |
|--------------------|---------------------------------------------------------------------|
| Speculative I/O    | Attempts synchronous I/O before async call to eliminate callback overhead |
| Gather Write       | Sends header+body in a single system call via writev()             |
| Zero-Copy Message  | Stores only user buffer pointer in msg_t, transmits without copy   |
| VSM (Inline)       | Messages <=33 bytes stored directly in msg_t internal buffer (no malloc) |
| Lock-free YPipe    | CAS operation-based inter-thread message exchange, no mutex        |
| Cache Line Opt.    | YPipe nodes aligned to cache line size                             |
| Backpressure       | Pauses reading when 10MB limit exceeded to prevent memory blowup   |
