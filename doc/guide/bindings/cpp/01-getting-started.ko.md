[C++ 가이드](./index.ko.md) · [다음: 메시징 →](./02-messaging.ko.md)

# 시작하기

## 빌드 연동

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

## 5분 예제 — PING/ACK

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

## 소유권 규칙

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
