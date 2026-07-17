[← 목차](README.ko.md)

# 10. Stream

## 1. stream이 하는 일

stream은 **게임 클라이언트 같은 외부 접속자를 받는 양방향 연결**이다. 채널이
서버 간 메시징이라면, stream은 서버-클라이언트 경계다. 서버 쪽은 stream
node + session으로 받고, 클라이언트 쪽은 별도 산출물인 **stream connector**로
접속한다.

```text
Game client --(stream connector)--> stream node --> session --> actor/spot
```

## 2. 서버: stream node 선언

```cpp
options.add_stream_node ("tictactoe.stream")
  .bind (topology.stream_endpoint)            // 예: tcp://0.0.0.0:5581
  .enable_actor_dispatch ("game")             // session Actor를 소유한 MeshName
  .register_session<play_session_t> ()
```

이 예제는 옵션 레벨의 `stream_node_options_builder_t` 표면이다.

| 옵션 레벨 빌더 | 의미 |
|----------------|------|
| `bind(endpoint)` | 클라이언트 접속을 받을 endpoint |
| `set_tls_server(certificate,key[,require_client_certificate])` | TLS server 인증서와 키 설정 |
| `enable_actor_dispatch(mesh_name)` | session Actor dispatch에 사용할 local MeshNode 선택 |
| `register_session<T>()` | 연결당 생성될 session 타입 |

저수준 `stream_builder_t`는 같은 stream node를 구성하지만 session 타입을 직접
등록하지 않고 `register_session(session_name)`으로 이미 등록된 session 이름을
연결한다. 저수준 표면은 `bind(endpoint)`와 `register_session(session_name)`만 제공한다.

## 3. session 작성

연결 하나마다 session 인스턴스가 하나 만들어진다.
`packet_stream_session_t`를 상속하고 4개 훅을 구현한다. DI는 핸들러와 같은
`dependency_types` 방식이다.

```cpp
class play_session_t final : public zlink::framework::packet_stream_session_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::session_actor_manager_t,
                                          authenticate_play_session_handler_t>;

    play_session_t (zlink::framework::session_actor_manager_t &actors,
                    authenticate_play_session_handler_t &authenticate);

    zlink::framework::task_t<void> on_connected (zlink::framework::stream_t &) override;
    zlink::framework::task_t<void> on_disconnected (zlink::framework::stream_t &) override;
    zlink::framework::task_t<void> on_error (zlink::framework::stream_t &,
                                             const zlink::framework::stream_error_t &) override;
    zlink::framework::task_t<void> on_packet (zlink::framework::stream_t &stream,
                                              const zlink::framework::stream_dispatch_context_t &dispatch,
                                              const zlink::message_t &payload) override;
};
```

| 훅 | 시점 |
|----|------|
| `on_connected(stream)` | 연결 수립 |
| `on_packet(stream, dispatch, payload)` | 패킷 수신 — `dispatch.packet_name()`으로 분기 |
| `on_error(stream, error)` | 전송 오류 |
| `on_disconnected(stream)` | 연결 종료 — actor unbind 등 정리 |

```mermaid
stateDiagram-v2
    direction LR
    state "연결됨" as connected
    state "인증됨 (actor 바인딩)" as authenticated
    [*] --> connected: on_connected
    connected --> authenticated: authenticate 패킷
    authenticated --> authenticated: on_packet → relay (9장)
    connected --> [*]: on_disconnected
    authenticated --> [*]: on_disconnected → binding cleanup
```

전형적인 `on_packet` 패턴은 [9장 §4](09-actor-session.ko.md#4-session--actor-바인딩)에 있다:
인증 패킷이면 인증 → actor 바인딩, 그 외에는 바인딩된 actor로
`relay(payload)`.

동기 완료만 필요한 훅은 완료된 task를 바로 돌려준다.

```cpp
zlink::framework::task_t<void> on_connected (zlink::framework::stream_t &) override
{
    return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
}
```

서버에서 클라이언트로 직접 쓰기는 `stream_t`의 write call을 쓴다.
`stream_t`는 현재 연결의 `session_id()`를 읽고, `close()`로 연결을 닫고,
write call과 reply call로 패킷을 쓴다. application은 STREAM header를 만들지 않는다.

```cpp
auto payload = zlink::message_t::from_json (response);
stream.write_packet (payload)
  .packet_name ("game.state.changed") // client가 받을 packet 이름
  .submit ();
stream.reply_packet (payload).submit (); // 현재 request dispatch에 대한 응답
```

STREAM session은 `stream_dispatch_context_t`와 `zlink::message_t` payload를 받는다.
header 객체를 application code가 만들거나 reply/relay 호출에 다시 넘기지 않는다. custom
codec은 기존처럼 builder/options에 등록하고, session 업무 코드는 packet name과 metadata만
dispatch context에서 읽는다.

## 4. 클라이언트: stream connector

클라이언트 쪽 접속은 `zlink::stream_connector` 산출물이 담당한다 — 프레임워크
서버 가이드 범위 밖이므로 여기서는 접점만 짚는다.

```cpp
// TicTacToe 클라이언트 시나리오의 실제 흐름 (요약)
auto client = /* stream connector 생성, room.owner_play_endpoint로 접속 */;
auto auth = co_await client.request (authenticate_req_t{token}).async<authenticate_res_t> ();
auto notify = co_await client.wait_for<game_state_notify_t> ().async ();   // 서버 알림 수신
```

connector의 계약과 사용법은
[stream connector 가이드](../../../stream-connector/cpp/guide/INDEX.ko.md)를
본다. 동작 예제는 [samples/TicTacToe/Client](../../../../languages/cpp/samples/TicTacToe/Client)가 기준이다.

connector도 framework처럼 **custom codec**을 사용할 수 있다. codec extension이 제공하는
`typed_codec_t` 구현을 `connector_options_t::typed_codec`에 한 번 넣으면 typed send, request와
수신이 모두 같은 codec을 사용한다. server framework 쪽 등록(`codecs().use(extension)`)과
대칭이며, 두 표면의 전체 목록은
[framework-api §9](../../spec/05-framework-api.ko.md#9-codec) 표를 본다.

```cpp
connector_options_t options;
options.endpoint = "tcp://game.example.com:9100";
options.typed_codec = avro_typed_codec; // extension package가 제공한 shared_ptr<const typed_codec_t>
auto client = connector_factory_t::create(std::move(options));
```

## 5. 패킷 계약

stream 패킷도 채널 메시지와 같은 typed DTO(`packet_name`)다. 서버 session은
`dispatch.packet_name()`으로 어떤 DTO인지 식별하고, payload를 해당 타입으로
디코딩한다. stream codec 값은 runtime 내부 header에 있으며 현재 값은 `raw`, `json`,
`message_pack`, `protobuf`다. spot까지 relay되는 패킷은 spot의
`add_actor_packet<&T::method>()` 등록과 만나 typed 핸들러로 dispatch된다
([8장 §3](08-spot.ko.md#3-room-spot-작성)).

## 6. 자주 막히는 곳

- **session이 만들어지지 않는다** → stream node에 `register_session<T>()`가 없거나 `bind`를
  설정하지 않았다. session actor가 필요하면 같은 구성 안에 stream session의 actor를 소유할 MeshNode와
  ChannelName이 있는지 확인한다([9장 §5](09-actor-session.ko.md#5-actor-gateway)).
- **패킷 디코딩 실패** → `packet_name`과 codec 등록을 확인한다.
- **heartbeat·재연결·TLS 동작** → 이는 서버 session이 아니라 클라이언트
  connector의 책임이다. [connector 가이드](../../../stream-connector/cpp/guide/INDEX.ko.md)가 다룬다.

## 7. 더 보기

- 클라이언트 connector(연결·heartbeat·재연결·TLS): [connector 가이드](../../../stream-connector/cpp/guide/INDEX.ko.md)
- 인터페이스/계약 카탈로그: [13장 인터페이스 카탈로그](13-interface-catalog.ko.md)
- 실행 가능한 전체 예제: [14장 샘플 맵](14-samples-map.ko.md)
- session ↔ actor 바인딩·게이트웨이: [9장 Actor · Session](09-actor-session.ko.md)

[다음: Registry →](11-registry.ko.md)
