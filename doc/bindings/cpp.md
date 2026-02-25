[English](cpp.md) | [한국어](cpp.ko.md)

# C++ Binding

## 1. Overview

- **Header-only**: Use a single `include/zlink.hpp`
- **RAII pattern**: Resource management via constructors/destructors
- **Requirement**: C++11 or later

## 2. Main Classes

| Class | C API Counterpart | Description |
|-------|-------------------|-------------|
| `context_t` | `zlink_ctx_*` | Context |
| `socket_t` | `zlink_socket/close/bind/connect/send/recv` | Socket |
| `message_t` | `zlink_msg_*` | Message |
| `poller_t` | `zlink_poll` | Event poller |
| `monitor_t` | `zlink_socket_monitor_*` | Monitor |

## 3. Basic Example

```cpp
#include <zlink.hpp>
#include <iostream>

int main() {
    zlink::context_t ctx;

    // PAIR server
    zlink::socket_t server(ctx, ZLINK_PAIR);
    server.bind("tcp://*:5555");

    // PAIR client
    zlink::socket_t client(ctx, ZLINK_PAIR);
    client.connect("tcp://127.0.0.1:5555");

    // Send
    zlink::message_t msg("Hello", 5);
    client.send(msg);

    // Receive
    zlink::message_t reply;
    server.recv(reply);
    std::cout << std::string((char*)reply.data(), reply.size()) << std::endl;

    return 0;
}
```

## 4. DEALER/ROUTER Example

```cpp
zlink::context_t ctx;
zlink::socket_t router(ctx, ZLINK_ROUTER);
router.bind("tcp://*:5555");

zlink::socket_t dealer(ctx, ZLINK_DEALER);
dealer.connect("tcp://127.0.0.1:5555");

// Send
dealer.send(zlink::message_t("request", 7));

// Receive (routing_id + data)
zlink::message_t id, body;
router.recv(id);
router.recv(body);

// Reply
router.send(id, ZLINK_SNDMORE);
router.send(zlink::message_t("reply", 5));
```

## 5. Build

```cmake
# CMakeLists.txt
find_library(ZLINK_LIB zlink)
target_link_libraries(myapp ${ZLINK_LIB})
target_include_directories(myapp PRIVATE path/to/zlink.hpp)
```

## 6. Native Libraries

Platform-specific binaries are provided in the `bindings/cpp/native/` directory:
- `linux-x86_64/libzlink.so`
- `linux-aarch64/libzlink.so`
- `darwin-x86_64/libzlink.dylib`
- `darwin-aarch64/libzlink.dylib`
- `windows-x86_64/zlink.dll`
- `windows-aarch64/zlink.dll`

## 7. STREAM Callback API

`socket_t` STREAM helpers:
- `stream_attach(zlink_stream_on_packets_fn, int flags)`
- `stream_attach(zlink_stream_on_packets_fn, stream_dispatch_mode mode)`
- `stream_detach()`
- `stream_send(...)`

Mode rules:
- While attached, consume STREAM payloads in the callback.
- Do not mix `recv()` for STREAM payload consumption while attached.
- After `stream_detach()`, normal `recv()` use is available again.

```cpp
int on_packets(const zlink_routing_id_t *rid, zlink_msg_t *msgs, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const void *data = zlink_msg_data(&msgs[i]);
        size_t size = zlink_msg_size(&msgs[i]);
        stream.stream_send(*rid, data, size, zlink::send_flag::none);
        zlink_msg_close(&msgs[i]);
    }
    return 0;
}

stream.stream_attach(on_packets, zlink::stream_dispatch_mode::len32be);
```
