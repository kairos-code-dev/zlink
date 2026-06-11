[← 목차](./README.ko.md)

# 5. 채널 메시징

## 1. 채널이 하는 일

채널은 이름을 가진 메시징 경로다. 서버 프로세스끼리 typed 메시지를 주고받는
기본 수단이며, endpoint 연결·재접속·직렬화·디스패치는 런타임이 처리한다.

| 종류 | 선언 | 패턴 |
|------|------|------|
| client/server | `add_client_server_channel(name)` | request-reply, 단방향 send |
| fanout | `add_fanout_channel(name)` | publisher → 다수 subscriber (topic) |
| dealer mesh | `add_dealer_mesh_channel(name)` | 동격 노드 간 분산 |
| route mesh | `add_route_mesh_channel(name)` | SPOT 라우팅 백본 ([6장](./06-spot.ko.md)) |

## 2. 서버 쪽: 핸들러 그룹과 채널

핸들러([3장 §4](./03-concepts.ko.md))를 그룹에 등록하고, 채널이 그룹을 가져다
쓴다.

```cpp
app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
    options.handlers ()
      .add<create_game_handler_t> ("play")
      .add<ensure_player_actor_handler_t> ("play");

    options.codecs ()
      .add_message_pack ()
      .add_message_pack<create_game_req_t> ()
      .add_message_pack<create_game_res_t> ();

    options.add_client_server_channel ("tictactoe.play")
      .enable_server ("tcp://0.0.0.0:5561")
      .use_handler_group ("play");
});
```

- **codec** — 채널 메시지의 직렬화 형식. `add_json()` / `add_message_pack()` /
  `add_protobuf()` 중 선택하고, message_pack은 DTO별 typed 등록
  (`add_message_pack<T>()`)을 함께 한다.
- 같은 그룹을 여러 채널이 공유할 수 있고, 한 채널에 그룹 하나를 연결한다.

## 3. 클라이언트 쪽: channel_client_t

요청을 보내는 프로세스는 채널을 client로 연결하고, `channel_client_t`를
주입받아 호출한다.

```cpp
options.add_client_server_channel ("tictactoe.play")
  .enable_client ("tcp://10.30.1.15:5561");   // endpoint 직접 지정
// 또는 registry discovery로 endpoint를 찾게 한다 (10장)
options.add_client_server_channel ("tictactoe.play").enable_client ();
```

`channel_client_t`는 DI 서비스다 — 핸들러의 `dependency_types`로 받는다.

```cpp
class create_game_http_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    explicit create_game_http_handler_t (zlink::framework::channel_client_t &client) :
        _client (client) {}

    zlink::framework::task_t<create_game_http_res_t>
    handle (const create_game_http_req_t &request)
    {
        auto room = co_await _client
                      .request<create_game_res_t> ("tictactoe.play",
                                                   create_game_req_t{request.game_name})
                      .async ();
        co_return create_game_http_res_t{room.room_id, room.game_name};
    }
    // ...
};
```

### call 표면

| facade | 호출 | 의미 |
|--------|------|------|
| `channel_client_t` | `request<TReply>(channel, req)` | request-reply. `co_await ....async()`로 typed 응답 |
| `publisher_t` | `publish(channel, topic, event)` | fanout 채널로 topic publish (§4) |
| `message_bus_t` | `send(channel, msg)` | 응답 없는 단방향 전송 (advanced — `app.advanced().use_zlink`의 builder에서 `message_bus()`로 획득) |

`channel_client_t`는 DI로 주입받고, `publisher_t`/`message_bus_t`는
`zlink_builder_t`(`publisher()`/`message_bus()`)에서 얻는다.

call 객체에는 전송 전에 옵션을 얹을 수 있다.

```cpp
auto reply = co_await _client
               .request<create_game_res_t> ("tictactoe.play", create_game_req_t{name})
               .timeout (std::chrono::seconds (2))
               .metadata ("trace-id", trace_id)
               .async ();
```

실패는 `co_await`에서 `framework_exception_t`로 던져진다 —
[13장. 에러 처리는 3장 §5와 동일 모델](./03-concepts.ko.md).

## 4. fanout: publish/subscribe

알림처럼 한 곳에서 여러 구독자로 흘리는 메시지는 fanout 채널을 쓴다.

```cpp
// publisher 쪽 채널 선언
options.add_fanout_channel ("bingo.notifications")
  .enable_publisher ("tcp://0.0.0.0:5571");

// 보내기 — publisher_t로 topic 단위 publish
co_await publisher.publish ("bingo.notifications", "room-3187",
                            number_drawn_notify_t{state}).async ();

// subscriber 쪽 — 핸들러 그룹으로 받는다
options.handlers ().add<number_drawn_handler_t> ("notifications");
options.add_fanout_channel ("bingo.notifications")
  .use_handler_group ("notifications");
```

`publisher_t`를 얻는 경로는 두 가지다 — spot 코드라면 노드에
`attach_publisher(channel)`를 걸고 spot 쪽에서 주입받는 패턴([6장 §6](./06-spot.ko.md)),
일반 코드라면 `app.advanced().use_zlink([&](auto &z){ auto pub = z.publisher(); ... })`.

## 5. route mesh (고급)

SPOT 노드 간 라우팅 백본이 필요할 때만 쓴다. TicTacToe Play 서버의 실제 선언:

```cpp
options.add_route_mesh_channel ("tictactoe.router")
  .bind (topology.play_router_endpoint)
  .set_routing_id (topology.play_rid)
  .connect (topology.play_router_endpoint)
  .enable_spot_route_egress ("tictactoe.game.discovery");
```

SPOT과의 결합은 [6장](./06-spot.ko.md)의 `accept_routes_from_channel`에서
이어진다.

[다음: SPOT →](./06-spot.ko.md)
