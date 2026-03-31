[English](cpp.md) | [한국어](cpp.ko.md)

# C++ 바인딩

## 1. 개요

- `include/zlink.hpp` 하나로 쓰는 header-only 래퍼다.
- `message_t`, `socket_t`, `registry_t`, `discovery_t`, `spot_t`는 move-only
  RAII 핸들이다.
- 최소 요구사항은 C++11이다.

## 2. 주요 클래스

| 클래스 | C API 대응 | 설명 |
|--------|-----------|------|
| `context_t` | `zlink_ctx_*` | context option, shutdown, term |
| `message_t` | `zlink_msg_*` | bytes/string 변환과 RAII 소유권 |
| `socket_t` | `zlink_socket`, `zlink_send`, `zlink_recv` | raw multipart socket wrapper |
| `monitor_handle_t` | `zlink_socket_monitor_*`, `zlink_monitor_snapshot` | socket monitor |
| `service_monitor_handle_t` | `zlink_service_monitor_*`, `zlink_monitor_snapshot` | discovery/spot monitor |
| `registry_t` | `zlink_registry_*` | bind/snapshot/query 지원 registry wrapper |
| `registry_query_client_t` | `zlink_registry_query_*` | topology query client |
| `discovery_t` | `zlink_discovery_*` | fixed service-view discovery wrapper |
| `spot_node_t`, `spot_t` | `zlink_spot_node_*`, `zlink_spot_*` | unified spot 모델 |

## 3. 기본 raw socket 예제

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

`socket_t`는 `void*`/`std::string` 기반 send/recv convenience를 제공하지 않는다.
payload 변환은 `message_t::from_bytes()`, `from_string()`, `to_bytes()`,
`to_string()`로만 한다.

## 4. DEALER/ROUTER multipart 예제

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

## 5. 서비스 계층 요약

서비스 래퍼는 다음 표면만 노출한다.

- `registry_t`
- `registry_query_client_t`
- `discovery_t`
- `spot_node_t`
- `spot_t`

`spot_t`는 `spot_node_t`로부터만 생성한다. topology/lifecycle은
`spot_node_t`, publish/subscribe facade는 `spot_t`가 맡는다.

이 문서는 구형 `receiver_t`, `spot_pub_t`, `spot_sub_t`, STREAM 전용
`stream_attach*`/`stream_send*` API를 더 이상 설명하지 않는다.

## 6. 빌드와 검증

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

샘플 smoke 대상:

- `pair_recv`, `pair_callback`
- `pubsub_recv`, `pubsub_callback`
- `dealer_router_recv`, `dealer_router_callback`
- `stream_recv`, `stream_callback`
- `spot_recv`, `spot_callback`

## 7. 네이티브 라이브러리

`bindings/cpp/native/` 디렉토리에 플랫폼별 바이너리 제공:
- `linux-x86_64/libzlink.so`
- `linux-aarch64/libzlink.so`
- `darwin-x86_64/libzlink.dylib`
- `darwin-aarch64/libzlink.dylib`
- `windows-x86_64/zlink.dll`
- `windows-aarch64/zlink.dll`
