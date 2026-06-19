[← 목차](README.ko.md)

# 7. 채널 메시징

## 1. 채널이 하는 일

채널은 이름을 가진 메시징 경로다. 서버 프로세스끼리 typed 메시지를 주고받는
기본 수단이며, endpoint 연결·재접속·직렬화·dispatch는 런타임이 처리한다.

| 종류 | 선언 | 패턴 |
|------|------|------|
| client/server | `add_client_server_channel(name)` | request-reply, 단방향 send — ROUTER 서버에 DEALER 클라이언트 (DEALER=client, ROUTER=server) |
| fanout | `add_fanout_channel(name)` | publisher → 다수 subscriber (topic) |
| dealer mesh | `add_dealer_mesh_channel(name)` | dealer ↔ dealer — round-robin 분산 |
| route mesh | `add_route_mesh_channel(name)` | router ↔ router — routing id 로 주소 라우팅 (SPOT node 구성: [8장](08-spot.ko.md)) |

## 2. 서버 쪽: 핸들러 그룹과 채널

핸들러([3장 §6.1](03-concepts.ko.md))를 그룹에 등록하고, 채널이 그룹을 가져다
쓴다.

> **cpp 는 수동 등록만 제공한다.** 어트리뷰트·리플렉션 기반 자동 등록(어노테이션
> scan)은 cpp 언어 표준이 아니라 제공하지 않는다 — 핸들러는 항상 명시 registry
> (`handlers().add<T>(...)`, SPOT 은 `configure()` context)로 등록한다. dotnet·node·java
> 는 수동에 더해 어트리뷰트/데코레이터 자동 등록도 제공한다.

```cpp
app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
    options.handlers ()
      .add<create_game_handler_t> ("play")
      .add<ensure_player_actor_handler_t> ("play");

    options.codecs ().use (
      zlink::framework_codecs::messagepack<create_game_req_t,
                                           create_game_res_t> ());

    options.add_client_server_channel ("tictactoe.play")
      .enable_server ("tcp://0.0.0.0:5561")
      .use_handler_group ("play");
});
```

- **codec** — 채널 메시지의 직렬화 형식. JSON은 기본 codec으로 제공하며
  `add_json()` 또는 `add_json<T>()`로 명시 등록할 수 있다. MessagePack과 Protobuf는
  framework codec extension package를 참조한 뒤 `codecs().use(extension)`으로 등록한다.
- **커스텀 codec(Avro·Thrift 등)** — 기본 codec 외 포맷은
  extension 객체에서 `add_serializer<T>(serialize, deserialize)`를 호출해 등록한다.
  serialize는 업무 객체를 `message_t`(byte payload)로, deserialize는 그 반대로 변환하는
  함수다. packet name 결정과 handler/client API는 codec 변경과 분리된다.

  ```cpp
  options.codecs ().add_serializer<place_order_t> (
    [schema] (const place_order_t &order) {
        return zlink::message_t::from (avro_encode (schema, order));
    },
    [schema] (const zlink::message_t &message) {
        return avro_decode<place_order_t> (schema, message.to_string ());
    });
  ```

  다른 언어의 등록 표면은 [framework-api §2.2](../../common/spec/framework-api.ko.md) 표를
  본다. client connector 쪽은 `codec_traits<T>` 특수화로 같은 커스텀 codec을 끼운다.
- 같은 그룹을 여러 채널이 공유할 수 있고, 한 채널에 그룹 하나를 연결한다.

위 선언이 만들어 내는 것:

```mermaid
flowchart LR
    subgraph ApiProc["Api 서버 프로세스"]
        AC["channel client<br/>tictactoe.play"]:::channel
    end
    subgraph PlayProc["Play 서버 프로세스"]
        PS["channel server<br/>bind tcp://0.0.0.0:5561"]:::channel
        subgraph HG["handler group: play"]
            H1["create_game_handler_t"]
            H2["ensure_player_actor_handler_t"]
        end
        PS -- "packet_name으로 라우팅" --> HG
    end
    AC == "request / reply<br/>(message_pack)" ==> PS

    classDef channel fill:#e3f2fd,stroke:#1565c0,color:#000000
```

채널 이름(`tictactoe.play`)이 양쪽을 잇는 키다. 서버는 endpoint에 bind하고
그룹의 핸들러로 dispatch하며, 클라이언트는 같은 이름으로 연결해 typed 요청을
보낸다.

## 3. 클라이언트 쪽: channel_client_t

요청을 보내는 프로세스는 채널을 client로 연결하고, `channel_client_t`를
주입받아 호출한다.

```cpp
options.add_client_server_channel ("tictactoe.play")
  .enable_client ("tcp://10.30.1.15:5561");   // endpoint 직접 지정
// 또는 registry discovery로 endpoint를 찾게 한다 (11장)
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
                      .request ("tictactoe.play", create_game_req_t{request.game_name})
                      .async<create_game_res_t> ();
        co_return create_game_http_res_t{room.room_id, room.game_name};
    }
    // ...
};
```

### call 표면

| facade | 호출 | 의미 |
|--------|------|------|
| `channel_client_t` | `request(channel, req).async<TReply>()` | request-reply. `co_await`로 typed 응답 |
| `publisher_t` | `publish(channel, topic, event)` | fanout 채널로 topic publish (§6) |
| `message_bus_t` | `send(channel, msg)` | 응답 없는 단방향 전송 (advanced — `app.advanced().zlink()`의 builder에서 `message_bus()`로 획득) |

`channel_client_t`는 DI로 주입받고, `publisher_t`/`message_bus_t`는
`zlink_builder_t`(`publisher()`/`message_bus()`)에서 얻는다.

call 객체에는 전송 전에 옵션을 얹을 수 있다.

```cpp
auto reply = co_await _client
               .request ("tictactoe.play", create_game_req_t{name})
               .metadata ("trace-id", trace_id)
               .async<create_game_res_t> ();
```

`.timeout(...)`을 생략해도 **무기한 대기하지 않는다.** framework 호출 객체의
기본 timeout 값인 0ms가 native request-reply 경로로 전달되고, native 기본
timeout(현재 **5초**)이 적용된다. 이 호출만 더 짧게/길게 두고 싶을 때 위처럼
호출 단위로 지정한다.

request 한 번의 전체 흐름 — 디코딩/인코딩과 핸들러 호출은 런타임이 처리하고,
양쪽 application 코드는 typed DTO만 본다.

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {'signalTextColor': '#000000', 'actorTextColor': '#000000', 'noteTextColor': '#000000', 'actorBkg': '#ffffff', 'actorBorder': '#555555', 'activationBorderColor': '#555555'}}}%%
sequenceDiagram
    participant App as Api: 핸들러 코드
    participant CC as channel client
    participant PS as Play: channel server
    participant H as handler group "play"

    App->>CC: co_await request(...).async<create_game_res_t>()
    Note over App: suspend — 스레드 비점유 (3장 §6.2)
    CC->>PS: create_game_req (message_pack 인코딩)
    PS->>H: packet_name "CreateGame" → 디코딩 후 handle()
    H-->>PS: create_game_res
    PS-->>CC: reply 인코딩 전송
    CC-->>App: resume — typed create_game_res_t
```

실패는 `co_await`에서 `framework_exception_t`로 던져진다 —
에러 처리는 [3장 §6.2](03-concepts.ko.md)와 동일 모델이다.

## 4. filter — 공통 처리

HTTP middleware(`options.http().use<TMiddleware>()`)는 HTTP route 파이프라인 전용이다.
ZLink channel handler에는 자동으로 적용되지 않는다. channel handler 앞뒤의 로깅,
검증, 권한 확인, metric 기록처럼 여러 handler에 반복되는 처리는 handler filter로 둔다.

```cpp
class audit_filter_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::logger_t<audit_filter_t>>;

    explicit audit_filter_t (zlink::framework::logger_t<audit_filter_t> &logger) :
        _logger (logger) {}

    zlink::framework::task_t<zlink::message_t>
    invoke (const zlink::framework::handler_invocation_context_t &invocation,
            zlink::framework::handler_next_t next)
    {
        _logger.info ("dispatch zlink handler",
                      {{"packet", invocation.context.packet_name}});

        auto reply = co_await next ();   // 호출하지 않으면 handler는 실행되지 않는다.

        co_return reply;
    }

  private:
    zlink::framework::logger_t<audit_filter_t> &_logger;
};

app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
    options.use_filter<audit_filter_t> ();

    options.handlers ()
      .add<create_game_handler_t> ("play")
      .add<ensure_player_actor_handler_t> ("play");
});
```

filter 타입은 `invoke(const handler_invocation_context_t &, handler_next_t)`를 제공한다.
`handler_invocation_context_t`에는 handler descriptor, channel/packet 이름 같은 dispatch
context, 변경할 수 없는 raw message payload가 들어 있다. 처리를 계속하려면 `next()`를
`co_await`하고, handler 호출을 막아야 하는 경우에는 `next()`를 호출하지 않고 직접
`message_t`를 반환한다.

filter도 framework가 직접 `new`로 만들지 않는다. `options.use_filter<TFilter>()`로
등록하면 같은 DI 컨테이너에서 resolve되며, 등록한 순서대로 handler 호출 앞단을 감싼다.

## 5. dealer mesh: 외부 로드밸런서 없이 수평 확장

처리량을 늘리고 싶을 때 같은 채널에 서버 인스턴스를 추가하면 된다. 별도 nginx·HAProxy 없이 클라이언트 요청이 자동으로 분산된다.

```cpp
// 이미지 처리 서버 A
options.add_dealer_mesh_channel ("image.resize")
    .enable_server ("tcp://0.0.0.0:5600")
    .use_handler_group ("resize");

// 이미지 처리 서버 B — 동일 채널, 다른 프로세스
options.add_dealer_mesh_channel ("image.resize")
    .enable_server ("tcp://0.0.0.0:5601")
    .use_handler_group ("resize");
```

```cpp
// 클라이언트: 두 서버에 연결. 요청은 라운드로빈으로 분산됨
options.add_dealer_mesh_channel ("image.resize")
    .enable_client ("tcp://10.30.1.10:5600")
    .enable_client ("tcp://10.30.1.10:5601");

// 또는 discovery로 자동 발견 — 서버가 추가될 때 클라이언트 재시작 불필요
options.add_dealer_mesh_channel ("image.resize")
    .enable_client ();   // 인자 없는 enable_client = registry discovery로 자동 연결
```

dealer mesh는 DEALER ↔ DEALER 대칭이라 **한 노드가 server와 client를 둘 다** 할 수
있다 — `enable_server(endpoint)`로 받으면서(제공) `enable_client(...)`로 다른 peer에
보낸다(소비). 둘은 같은 DEALER socket을 공유한다.

```mermaid
flowchart LR
    C["클라이언트<br/>dealer_mesh client"]:::channel
    S1["이미지 서버 A<br/>bind 5600"]:::channel
    S2["이미지 서버 B<br/>bind 5601"]:::channel
    S3["이미지 서버 C<br/>bind 5602<br/>(동적 추가)"]:::channel

    C == "요청 1" ==> S1
    C == "요청 2" ==> S2
    C == "요청 3" ==> S1
    C -. "서버 추가 후<br/>자동 발견" .-> S3

    classDef channel fill:#e3f2fd,stroke:#1565c0,color:#000000
```

핸들러는 client/server 채널과 동일한 구조다. request handler는
`request_type`/`reply_type`/`topic_name` + `handle()`을 두고, send handler는
`message_type` + `handle()`을 둔다. 채널 선언만 `add_dealer_mesh_channel`로
바꾸면 된다.

> **route mesh와 차이**: dealer mesh는 어느 서버에나 요청을 보낼 수 있는 stateless 서비스에 적합하다. 특정 엔티티(주문 ID, 사용자 ID 등)가 항상 같은 서버로 가야 한다면 route mesh([§7](#7-route-mesh-고급))를 쓴다.

## 6. fanout: publish/subscribe

알림처럼 한 곳에서 여러 구독자로 흘리는 메시지는 fanout 채널을 쓴다.

```cpp
// publisher 쪽 채널 선언
options.add_fanout_channel ("bingo.notifications")
  .enable_publisher ("tcp://0.0.0.0:5571");

// 보내기 — publisher_t로 topic 단위 publish
co_await publisher.publish ("bingo.notifications", "room-3187",
                            number_drawn_notify_t{state}).async ();

// subscriber 쪽 — 핸들러 그룹으로 받는다
options.handlers ().add_publish<number_drawn_handler_t> ("notifications");
options.add_fanout_channel ("bingo.notifications")
  .enable_subscriber ("tcp://10.30.1.20:5571")
  .use_handler_group ("notifications");
```

`publisher_t`는 `auto pub = app.advanced().zlink().publisher();`의
`zlink_builder_t::publisher()`에서 얻는다. SPOT 코드에서 fanout으로 발행하려면
노드에 `attach_publisher(channel)`를 걸고 `spot_context_t::publish(topic, event)`를
사용한다([8장 §6](08-spot.ko.md)).

```mermaid
flowchart LR
    P["publisher<br/>bind tcp://0.0.0.0:5571"]:::channel
    S1["subscriber A<br/>handler group: notifications"]:::channel
    S2["subscriber B<br/>handler group: notifications"]:::channel
    S3["subscriber C<br/>(topic 미구독 — 수신 없음)"]:::infra
    P -- "topic: room-3187" --> S1
    P -- "topic: room-3187" --> S2
    P -.->|"room-9920만 구독"| S3

    classDef channel fill:#e3f2fd,stroke:#1565c0,color:#000000
    classDef infra fill:#eceff1,stroke:#546e7a,color:#000000
```

publish 한 message 는 **구독자 수와 무관하게 한 번만 인코딩**된다. 런타임은 그
인코딩 결과를 **구독 중인 각 구독자 연결로 한 번씩 전달**할 뿐, 구독자마다 다시
직렬화하지 않는다.

## 7. route mesh (고급)

route mesh 는 **router ↔ router** 연결로, `routing_id` 를 지정해 **특정 주소로
라우팅**한다(dealer 의 round-robin 분산과 대비). dealer mesh 와 똑같이 한 노드가
**server 와 client 를 둘 다** 한다 — `enable_server(endpoint)` 로 bind 해서 받고(제공),
`enable_client(endpoint)` 로 다른 router 에 연결한다(소비). 둘은 같은 ROUTER socket을
공유한다. SPOT 노드가 이 route mesh 로 구성되므로, SPOT 라우팅 백본이 필요할 때 쓴다.
TicTacToe Play 서버 선언:

```cpp
options.add_route_mesh_channel ("tictactoe.router")
  .enable_server (topology.play_router_endpoint)   // 이 ROUTER bind(제공)
  .set_routing_id (topology.play_rid)              // 이 router 의 주소
  .enable_spot_route_egress ("tictactoe.game.discovery");
```

SPOT과의 결합은 [8장](08-spot.ko.md)의 `accept_routes_from_channel`에서
이어진다.

[다음: SPOT →](08-spot.ko.md)
