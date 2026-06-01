[← 운영](./04-operations.ko.md) · [C++ 가이드](./index.ko.md)

# 레퍼런스

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

## C API ↔ C++ 대응표

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

## API 레퍼런스 생성 (Doxygen)

```bash
cd bindings/cpp
doxygen Doxyfile
# 또는 CMake 빌드 디렉터리에서
cmake --build build --target doc
```

생성된 HTML은 `bindings/cpp/build/docs/html/index.html`에 있습니다.

---

## 샘플 목록

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
