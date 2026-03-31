[English](cpp.md) | [한국어](cpp.ko.md)

# C++ Binding

## 1. Overview

- Header-only wrapper exposed through `include/zlink.hpp`.
- `message_t`, `socket_t`, `registry_t`, `discovery_t`, and `spot_t` are
  move-only RAII handles.
- Requires C++11 or later.

## 2. Main Classes

| Class | C API Counterpart | Description |
|-------|-------------------|-------------|
| `context_t` | `zlink_ctx_*` | Context options, shutdown, term |
| `message_t` | `zlink_msg_*` | RAII message with bytes/string helpers |
| `socket_t` | `zlink_socket`, `zlink_send`, `zlink_recv` | Raw multipart socket wrapper |
| `monitor_handle_t` | `zlink_socket_monitor_*`, `zlink_monitor_snapshot` | Socket monitor |
| `service_monitor_handle_t` | `zlink_service_monitor_*`, `zlink_monitor_snapshot` | Discovery/spot monitor |
| `registry_t` | `zlink_registry_*` | Registry bind/snapshot/query wrapper |
| `registry_query_client_t` | `zlink_registry_query_*` | Topology query client |
| `discovery_t` | `zlink_discovery_*` | Fixed service-view discovery wrapper |
| `spot_node_t`, `spot_t` | `zlink_spot_node_*`, `zlink_spot_*` | Unified spot model |

## 3. Basic Raw Socket Example

```cpp
#include <zlink.hpp>
#include <iostream>

int main() {
    zlink::context_t ctx;

    zlink::socket_t server(ctx, zlink::socket_type::pair);
    server.bind("tcp://*:5555");

    zlink::socket_t client(ctx, zlink::socket_type::pair);
    client.connect("tcp://127.0.0.1:5555");

    zlink::message_t msg = zlink::message_t::from_string("Hello");
    client.send(msg);

    zlink::message_t inbound;
    server.recv(inbound);
    std::cout << inbound.to_string() << std::endl;

    return 0;
}
```

`socket_t` intentionally does not expose `void *` or `std::string` send/recv
convenience overloads. Payload conversion lives on `message_t` through
`from_bytes()`, `from_string()`, `to_bytes()`, and `to_string()`.

## 4. DEALER/ROUTER Multipart Example

```cpp
zlink::context_t ctx;
zlink::socket_t router(ctx, zlink::socket_type::router);
router.bind("tcp://*:5555");

zlink::socket_t dealer(ctx, zlink::socket_type::dealer);
dealer.connect("tcp://127.0.0.1:5555");

zlink::message_t request = zlink::message_t::from_string("request");
dealer.send(request);

zlink_routing_id_t rid;
zlink::message_t body;
router.recv(rid, body);

zlink::message_t reply = zlink::message_t::from_string("reply");
router.send(rid, reply);
```

## 5. Service Layer Summary

The service wrappers are intentionally limited to:

- `registry_t`
- `registry_query_client_t`
- `discovery_t`
- `spot_node_t`
- `spot_t`

`spot_t` is constructed from `spot_node_t` only. `spot_node_t` owns
topology/lifecycle, while `spot_t` is the publish/subscribe facade.

This binding no longer documents legacy `receiver_t`, `spot_pub_t`,
`spot_sub_t`, or STREAM-only `stream_attach*` and `stream_send*` helpers.

## 6. Build And Verification

```cmake
# CMakeLists.txt
find_library(ZLINK_LIB zlink)
target_link_libraries(myapp ${ZLINK_LIB})
target_include_directories(myapp PRIVATE path/to/zlink.hpp)
```

```bash
./bindings/cpp/build.sh ON ON
ctest --test-dir core/build --output-on-failure -L contract
ctest --test-dir core/build --output-on-failure -L sample-smoke -j1
```

Sample smoke coverage:

- `pair_recv`, `pair_callback`
- `pubsub_recv`, `pubsub_callback`
- `dealer_router_recv`, `dealer_router_callback`
- `stream_recv`, `stream_callback`
- `spot_recv`, `spot_callback`

## 7. Native Libraries

Platform-specific binaries are provided in the `bindings/cpp/native/` directory:
- `linux-x86_64/libzlink.so`
- `linux-aarch64/libzlink.so`
- `darwin-x86_64/libzlink.dylib`
- `darwin-aarch64/libzlink.dylib`
- `windows-x86_64/zlink.dll`
- `windows-aarch64/zlink.dll`
