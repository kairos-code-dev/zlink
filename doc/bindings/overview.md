[English](overview.md) | [한국어](overview.ko.md)

# Bindings Common Overview

## 1. Overview

zlink provides language bindings for 5 languages based on the C API. All bindings share the same concepts, method names, and error conventions.

## 2. Binding Selection Guide

| Binding | Minimum Requirement | Features | Best For |
|---------|---------------------|----------|----------|
| C++ | C++11 | Header-only RAII | Same process as core |
| Java | Java 22+ | FFM API (no JNI) | JVM-based services |
| .NET | .NET 8+ | LibraryImport | C#/F# services |
| Node.js | Node 20+ | N-API, prebuilds | Web servers/tools |
| Python | Python 3.9+ | ctypes/CFFI | Scripting/prototyping |

## 3. Common API Mapping

| Concept | C++ | .NET | Java | Node.js | Python |
|---------|-----|------|------|---------|--------|
| Create Context | `context_t()` | `Context()` | `new Context()` | `new Context()` | `Context()` |
| Terminate Context | `~context_t()` | `Dispose()` | `close()` | `close()` | `close()` |
| Create Socket | `socket_t(ctx, type)` | `new Socket(ctx, type)` | `ctx.createSocket(type)` | `ctx.socket(type)` | `ctx.socket(type)` |
| bind | `bind()` | `Bind()` | `bind()` | `bind()` | `bind()` |
| connect | `connect()` | `Connect()` | `connect()` | `connect()` | `connect()` |
| close | `close()` | `Dispose()` | `close()` | `close()` | `close()` |
| send | `send(buf)` | `Send(byte[])` | `send(byte[])` | `send(Buffer)` | `send(bytes)` |
| recv | `recv(buf)` | `Recv(byte[])` | `recv(byte[])` | `recv()` | `recv()` |
| set option | `set()` | `SetOption()` | `setOption()` | `setOption()` | `setOption()` |
| get option | `get()` | `GetOption()` | `getOption()` | `getOption()` | `getOption()` |

## 4. Error Handling Conventions

- C++: Returns error codes (same as C API)
- .NET/Java/Node.js: Exception-based, internally maps errno
- Python: Exception-based

## 5. Message Ownership

- On successful send, ownership is transferred (message is emptied)
- On failed send, the caller retains ownership
- recv fills an internal buffer or returns an object

## 6. Thread Safety

- Public socket/service handles: **thread-safe** for same-handle operational
  APIs; `close`/`destroy` remain more restrictive and may fail with `EBUSY`
- Context: **thread-safe** (sockets can be created from multiple threads)

## 7. Version Policy

- Binding versions follow the core `VERSION`
- Bindings are updated simultaneously with C API changes

## 8. Naming Conventions

- Socket types use the same names as C API constants
- Options are exposed directly as `ZLINK_*` constants
- Only method names follow language conventions (PascalCase/camelCase)

## 9. STREAM Callback API Mapping

Common STREAM rules:
- Callback attached: receive in callback dispatch path.
- Callback detached/not attached: receive with normal `recv` path.
- There is no dedicated `stream_recv` C API.

| Binding | Attach | Detach | Peer Routing ID | Send |
|---|---|---|---|---|
| C++ | `socket_t::stream_attach(...)` | `socket_t::stream_detach()` | `socket_t::peer_routing_id(...)` | `socket_t::stream_send(...)` |
| .NET | `Socket.AttachStream(...)` | `Socket.DetachStream()` | `Socket.StreamPeerRoutingId(...)` | `Socket.StreamSend(...)` |
| Java | `Socket.attachStream(...)` | `Socket.detachStream()` | `Socket.streamPeerRoutingId(...)` | `Socket.streamSend(...)` |
| Node.js | `socket.streamAttach(...)` | `socket.streamDetach()` | `socket.streamPeerRoutingId(...)` | `socket.streamSend(...)` |
| Python | `socket.stream_attach(...)` | `socket.stream_detach()` | `socket.stream_peer_routing_id(...)` | `socket.stream_send(...)` |
