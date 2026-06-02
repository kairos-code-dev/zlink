[바인딩 가이드](../README.md) · [코어 가이드](../../01-overview.md)

# C++ 바인딩 가이드 (`zlink::`)

헤더 전용(header-only는 아님, 컴파일 링크) RAII 래퍼로 C 코어를 감싸며, C++에서 zlink를 사용하는 방법을 실제 샘플 코드 중심으로 설명합니다.

---

## 설치

C++ 바인딩은 CMake로 제공됩니다.

```cmake
add_subdirectory(bindings/cpp)
target_link_libraries(my_app PRIVATE zlink::zlink-cpp)
```

- **C++20** 이상 (coroutine, concepts 사용).
- 네이티브 코어가 함께 링크됩니다.

```cpp
#include <zlink.hpp>   // 모든 공개 API
```

---

## 5분 예제

```cpp
#include <zlink.hpp>

// 서버
zlink::context_t ctx;
zlink::pair_socket_t server (ctx);
server.bind ("tcp://127.0.0.1:5555");

zlink::received_t inbound;
server.recv (inbound);
std::printf ("%s\n", inbound.parts ()[0].to_string ().c_str ()); // PING
inbound.close ();

zlink::message_t ack = zlink::message_t::from ("ACK");
inbound.send ().message (ack).submit ();
```

```cpp
// 클라이언트
zlink::context_t ctx;
zlink::pair_socket_t client (ctx);
client.connect ("tcp://127.0.0.1:5555");

zlink::message_t ping = zlink::message_t::from ("PING");
client.send ().message (ping).submit ();

zlink::received_t inbound;
client.recv (inbound);
std::printf ("%s\n", inbound.parts ()[0].to_string ().c_str ()); // ACK
inbound.close ();
```

---

## 핵심 타입

### 컨텍스트

`context_t`는 RAII로 관리됩니다. 소멸자에서 자동으로 종료됩니다.

```cpp
{
    zlink::context_t ctx;
    zlink::pair_socket_t socket (ctx);
    // ...
} // ctx 소멸 시 하위 소켓의 블로킹 작업 중단
```

### 메시지

`message_t`는 페이로드 프레임 하나를 소유합니다. `send`로 전달하면 소유권이
이전(move)되며, 이후 사용 시 무효 상태입니다.

```cpp
// 문자열에서 생성
zlink::message_t msg = zlink::message_t::from ("payload");

// 바이트에서 생성
std::vector<uint8_t> bytes = {0x01, 0x02};
zlink::message_t msg = zlink::message_t::from (bytes);

// 크기 지정 빈 프레임
zlink::message_t msg = zlink::message_t::allocate (256);
std::memcpy (msg.data (), src, 256);

// 전송 — msg는 여기서 move됨
socket.send ().message (msg).submit ();
// 전송 후 msg는 무효 — 다시 쓰지 말 것
```

수신된 메시지 읽기:

```cpp
const zlink::message_t &part = inbound.parts ()[0];
std::string text = part.to_string ();              // 문자열 복사
std::span<const std::byte> bytes = part.bytes ();  // 뷰 (메시지 수명 동안만)
size_t size = part.size ();
```

### received_t — 수신 봉투

```cpp
zlink::received_t inbound;
int rc = socket.recv (inbound);   // 0 = 성공
// 또는 플래그 지정
socket.recv (inbound, zlink::recv_flags_t::none);

auto parts = inbound.parts ();                              // const vector
auto rid = inbound.routing_id ();                          // optional<routing_id_t>
auto seq = inbound.request_seq ();                         // optional<uint64_t>

inbound.close ();   // 명시적 해제 (또는 소멸자)
```

### 라우팅 ID

```cpp
auto rid = zlink::routing_id_t::from (
    reinterpret_cast<const uint8_t*> (text.data ()), text.size ());
socket.set_routing_id (rid);
```

---

## 소유권과 수명

| 상황 | 규칙 |
|------|------|
| `submit()` 성공 | `message_t`가 move됨 — 이후 사용 무효 |
| `submit()` 실패 | 예외(`submit_error_t`) 발생, 메시지 소유권 유지 |
| `recv()` | `received_t&`로 in-place 수신, `close()` 또는 소멸자로 해제 |
| 비동기 요청 | 회신 `std::vector<message_t>` 소유, 벡터 소멸 시 자동 해제 |

```cpp
try {
    zlink::message_t msg = zlink::message_t::from ("data");
    socket.send ().message (msg).submit ();  // 성공 시 msg move
} catch (const zlink::submit_error_t &e) {
    // 전송 실패 처리
}
```

---

## 에러 처리

C++ 바인딩은 `zlink::binding_error_t`를 상속하는 작업별 예외를 던집니다.

```cpp
try {
    zlink::message_t msg = zlink::message_t::from ("data");
    socket.send ().message (msg).submit ();
} catch (const zlink::submit_error_t &e) {
    if (e.result () == zlink::submit_result_t::backpressured) {
        // 재시도
    } else {
        throw;
    }
}
```

예외 타입:

| 예외 | 발생 시점 | result() 타입 |
|------|----------|---------------|
| `submit_error_t` | 전송/발행 실패 | `submit_result_t` |
| `request_error_t` | 요청 실패 | `request_result_t` |
| `recv_error_t` | 수신 실패 | `recv_result_t` |
| `bind_error_t` | 바인드 실패 | `bind_result_t` |
| `connect_error_t` | 연결 실패 | `connect_result_t` |
| `config_error_t` | 옵션 설정 실패 | `config_result_t` |
| `close_error_t` | 닫기 실패 | `close_result_t` |
| `handler_error_t` | 핸들러 등록 실패 | `handler_result_t` |

모두 `binding_error_t`를 상속하며 `code()`, `internal_errno()`로 네이티브 코드를
확인할 수 있습니다. 일부 recv API는 예외 대신 `recv_result_t` 정수 코드를 반환합니다
(예제 참고).

---

## C API 대응표

| C API | C++ API |
|-------|---------|
| `zlink_ctx_new()` | `zlink::context_t{}` |
| `zlink_ctx_term()` | 소멸자 또는 `ctx.term()` |
| `zlink_socket(ctx, type)` | `zlink::pair_socket_t{ctx}` 등 |
| `zlink_bind(s, ep)` | `socket.bind(ep)` |
| `zlink_connect(s, ep)` | `socket.connect(ep)` |
| `zlink_send_part(...)` | `socket.send().message(m).submit()` |
| `zlink_recv(...)` | `socket.recv(received)` |
| `zlink_msg_data(msg)` | `part.data()` / `part.bytes()` |
| `zlink_msg_size(msg)` | `part.size()` |
| `zlink_routing_id_t` | `zlink::routing_id_t` |
| `zlink_socket_monitor_open(...)` | `socket.monitor_open(...)` |
| `zlink_poller_new()` | `zlink::poller_t{}` |
| `zlink_timer_new()` | `zlink::timer_t{}` |
| `zlink_spot_node_new(ctx)` | `zlink::service::spot_node_t{ctx}` |
| `zlink_spot_node_create_spot(...)` | `node.create_spot()` |
| `zlink_spot_node_actor_new(...)` | `node.create_actor("id")` |
| `zlink_registry_new(ctx)` | `zlink::service::registry_t{ctx}` |
| `zlink_discovery_new(ctx,...)` | `zlink::service::discovery_t{ctx, type, ch}` |

---

## 네이티브 라이브러리 / 배포

```cpp
int major, minor, patch;
zlink::version (major, minor, patch);
std::printf ("zlink %d.%d.%d\n", major, minor, patch);

if (zlink::has ("draft")) {
    // draft API 지원
}
```

스레딩 규칙:

| 항목 | 규칙 |
|------|------|
| `context_t` | 스레드 간 공유 가능 |
| 소켓 | **하나의 스레드에서만** 사용. 동시 접근 금지 |
| 디스패치 핸들러 | zlink 내부 워커 스레드에서 호출됨 |
| `message_t::bytes()` | 메시지 수명 동안만 유효한 span |

```cpp
// 올바른 패턴: 소켓 per-스레드
std::thread worker ([&ctx] {
    zlink::dealer_socket_t socket (ctx);
    socket.connect ("tcp://...");
    // 이 스레드에서만 socket 사용
});
```

---

## 샘플

`bindings/cpp/samples/` 디렉터리의 검증된 샘플입니다.

| 파일 | 설명 |
|------|------|
| `pair_recv_sample.cpp` | PAIR 송수신 |
| `dealer_router_recv_sample.cpp` | DEALER/ROUTER 송수신 |
| `request_reply_async_sample.cpp` | std::future 비동기 요청/응답 |
| `pubsub_recv_sample.cpp` | XPUB/SUB 발행·구독 |
| `stream_recv_sample.cpp` | STREAM 원시 TCP |
| `stream_packet_callback_sample.cpp` | STREAM 패킷 콜백 |
| `monitor_recv_sample.cpp` | 모니터 이벤트 수신 |
| `discovery_registry_sample.cpp` | Registry + Discovery |
| `registry_query_sample.cpp` | Registry 토폴로지 쿼리 |
| `spot_recv_sample.cpp` | SpotNode/Spot PUB/SUB |
| `spot_request_async_sample.cpp` | SpotNode 비동기 요청 |
| `actor_single_player_queue_sample.cpp` | 액터 조인·이동·메시지 큐 |
| `actor_room_server_sample.cpp` | 방 서버 패턴 |
| `actor_gateway_relay_sample.cpp` | 게이트웨이 릴레이 |

```bash
cd bindings/cpp
cmake -B build && cmake --build build
./build/samples/pair_recv_sample
```

---

## 더 보기

- 소켓 패턴: [개요](../../03-0-socket-patterns.md) — [PAIR](../../03-1-pair.md) · [PUB/SUB](../../03-2-pubsub.md) · [DEALER](../../03-3-dealer.md) · [ROUTER](../../03-4-router.md) · [STREAM](../../03-5-stream.md) · [프록시](../../03-6-proxy.md)
- 서비스: [개요](../../07-0-services.md) — [discovery](../../07-1-discovery.md) · [SPOT](../../07-3-spot.md) · [Actor](../../07-4-actor.md) · [registry](../../07-4-registry.md)
- 운영: [소켓 옵션](../../12-socket-options.md) · [TLS](../../05-tls-security.md) · [모니터링](../../06-monitoring.md) · [스레드 안전성](../../11-thread-safety.md) · [메시지 API](../../09-message-api.md) · [라우팅 ID](../../08-routing-id.md)
