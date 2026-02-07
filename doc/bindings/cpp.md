# C++ 바인딩

## 1. 개요

- **Header-only**: `include/zlink.hpp` 하나로 사용
- **RAII 패턴**: 생성자/소멸자로 자원 관리
- **요구사항**: C++11 이상

## 2. 주요 클래스

| 클래스 | C API 대응 | 설명 |
|--------|-----------|------|
| `context_t` | `zlink_ctx_*` | 컨텍스트 |
| `socket_t` | `zlink_socket/close/bind/connect/send/recv` | 소켓 |
| `message_t` | `zlink_msg_*` | 메시지 |
| `poller_t` | `zlink_poll` | 이벤트 폴러 |
| `monitor_t` | `zlink_socket_monitor_*` | 모니터 |

## 3. 기본 예제

```cpp
#include <zlink.hpp>
#include <iostream>

int main() {
    zlink::context_t ctx;

    // PAIR 서버
    zlink::socket_t server(ctx, ZLINK_PAIR);
    server.bind("tcp://*:5555");

    // PAIR 클라이언트
    zlink::socket_t client(ctx, ZLINK_PAIR);
    client.connect("tcp://127.0.0.1:5555");

    // 전송
    zlink::message_t msg("Hello", 5);
    client.send(msg);

    // 수신
    zlink::message_t reply;
    server.recv(reply);
    std::cout << std::string((char*)reply.data(), reply.size()) << std::endl;

    return 0;
}
```

## 4. DEALER/ROUTER 예제

```cpp
zlink::context_t ctx;
zlink::socket_t router(ctx, ZLINK_ROUTER);
router.bind("tcp://*:5555");

zlink::socket_t dealer(ctx, ZLINK_DEALER);
dealer.connect("tcp://127.0.0.1:5555");

// 전송
dealer.send(zlink::message_t("request", 7));

// 수신 (routing_id + data)
zlink::message_t id, body;
router.recv(id);
router.recv(body);

// 응답
router.send(id, ZLINK_SNDMORE);
router.send(zlink::message_t("reply", 5));
```

## 5. 빌드

```cmake
# CMakeLists.txt
find_library(ZLINK_LIB zlink)
target_link_libraries(myapp ${ZLINK_LIB})
target_include_directories(myapp PRIVATE path/to/zlink.hpp)
```

## 6. 네이티브 라이브러리

`bindings/cpp/native/` 디렉토리에 플랫폼별 바이너리 제공:
- `linux-x86_64/libzlink.so`
- `linux-aarch64/libzlink.so`
- `darwin-x86_64/libzlink.dylib`
- `darwin-aarch64/libzlink.dylib`
- `windows-x86_64/zlink.dll`
- `windows-aarch64/zlink.dll`
