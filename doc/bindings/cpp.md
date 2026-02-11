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
