<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Spec -- ZLink Framework C++ STREAM](./cpp-stream.ko.md) | [다음: Draft -- ZLink Framework C++ SPOT Samples](../internals/spot-samples.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](../README.ko.md) | [C++ 정책](../internals/cpp-framework-policy.ko.md) | [Framework 인터페이스](./cpp-framework-interfaces.ko.md) | [channel](./cpp-channel-messaging.ko.md) | [SPOT](./cpp-spot.ko.md) | [STREAM](./cpp-stream.ko.md) | [Monitoring](./cpp-monitoring.ko.md) | [Registry](./cpp-registry.ko.md)

# Spec -- ZLink Framework C++ Interface Alignment

> 이 문서는 **구현 완료된 설계 계약**이다.
> 기존 `C++` adapter interface catalog를 standalone
> framework 정책에 맞춰 정렬하기 위한 문서다.
> 전체 public interface 설계의 기준은
> [Framework 인터페이스](./cpp-framework-interfaces.ko.md)다.

## 1. 역할

이 문서는 예전 `C++` adapter 초안에서 사용하던 낮은 수준의 handler class와 client
surface를 새 framework 정책으로 옮길 때 지켜야 할 기준을 정리한다.

새 `C++` framework의 canonical 표면은 아래 문서에 둔다.

- [C++ 정책](../internals/cpp-framework-policy.ko.md)
- [Framework 인터페이스](./cpp-framework-interfaces.ko.md)

따라서 이 문서는 별도의 API 계약을 추가하지 않는다. 기존 문서나 샘플에 남아 있던
낡은 이름을 어떤 새 표면으로 맞출지 설명하는 정렬 문서로만 사용한다.

## 2. 정렬 기준

기존 초안의 표면은 아래 기준으로 바꾼다.

| 기존 초안 표현 | 새 framework 기준 |
|----------------|-------------------|
| `app_t` builder chain 직접 조립 | `app_t::create()` 후 `add_zlink_framework(...)`의 options builder로 구성 |
| raw request/send/event handler class | application handler는 `handler_registry_t`의 typed handler 등록, SPOT handler는 `spot_context_t::handlers()`의 Spot member function 등록 |
| channel client 직접 주입 | `message_bus_t`, `request_client_t`, `publisher_t` DI 주입 |
| event publisher 전용 타입 | `publisher_t::publish(channel, topic, event)` |
| channel 전체 연결 설정 | capability builder의 `bind`, `connect`, `use_discovery` |
| spot 전용 publisher client | `spot_context_t` 또는 `publisher_t`의 channel/topic 표면 |
| target Spot 직접 호출 public client | actor 생성 또는 Entry Spot join 뒤 actor/session handle 사용 |
| session actor relay용 route mesh channel | `stream.attach_actor_gateway(...)`와 `session_actor_t::relay(...)` |
| raw timer callback | `spot_context_t::add_timer(...)`와 `timer_tick_t` metadata |

application handler owner는 `options.handlers().add<THandler>(...)`로 등록한 타입이어야 한다.
생성자 주입이 필요하면 handler 타입의 `dependency_types`에 의존 타입을 적는다. 이 규칙은
handler lifecycle과 shutdown 중 resolve 금지 같은 host 정책을 한곳에서 닫기 위해 필요하다.

SPOT handler는 별도 handler class로 등록하지 않는다. `spot_context_t::handlers()`는
`add_actor_packet<&room_spot_t::place_mark>()`처럼 Spot 객체의 member function만 받는다.
Spot은 상태와 동작을 함께 감싸는 단위이므로 handler class를 따로 만들면 상태 owner와
동작 owner가 분리되어 샘플과 실제 구현이 달라진다.
일반 Spot은 `zlink::framework::spot_t`, Entry Spot은 `zlink::framework::entry_spot_t`를
상속한다. framework는 타입 이름에서 Entry Spot 여부를 추론하지 않고
`add_entry_spot<TEntrySpot>()` 호출과 기반 타입으로 역할을 확인한다.

actor lifecycle은 handler registry 등록 대상이 아니다. user Spot의 join admission은
`on_actor_join(actor, message_t)` member callback이 처리하고, 반환값은 accepted 여부와
optional reply `message_t`를 담는다. accepted가 `true`일 때만 actor 위치를 user Spot으로
commit하고 `onJoinActor(actor)`를 호출한다. accepted가 `false`이면 위치를 바꾸지
않고 post-joined callback도 호출하지 않는다. Entry Spot에는 admission callback이 없으며,
commit 이후 `onJoinActor(actor)`와 `onLeaveActor(actor)`만 둔다.

create callback도 request를 단일 `message_t`로 받는다. create result는 `existing`,
`created`, `rejected` state와 optional reply `message_t`를 담는다. `spot_context_t::close()`는
현재 Spot을 닫는 표면이며, actor가 남아 있는 user Spot은 닫지 않고 실패를 반환한다. callback
안에서 close를 요청하면 현재 callback이 끝난 뒤 닫는다. C++ SPOT dispatch는 CAPI dispatch
event 후 recv 경계에서 이미 실행 직렬화되므로 close를 위해 별도 실행 직렬화 queue를 추가하지
않는다.

## 3. Handler 등록 기준

일반 사용자는 raw `message_t` handler class를 상속하지 않고, typed payload를 받는
handler 타입만 등록한다. topic 이름과 payload 타입은 handler type alias와 `topic_name`
상수에서 얻는다.

```cpp
app.add_zlink_framework([](auto &options) {
    options.codecs().add_json();
    options.add_client_server_channel("orders")
      .enable_server("tcp://0.0.0.0:7001")
      .use_handler_group("orders-api");
    options.handlers()
      .add<order_created_handler_t>("orders-api")
      .add<get_order_status_handler_t>("orders-api");
});
```

raw payload가 필요한 경우에만 `send_raw(...)` 같은 고급 extension을 사용한다. STREAM은
framework core에서 Header 기반 packet 방식만 지원하므로 raw stream session은 공개
표면에 두지 않는다. 일반 샘플은 typed handler registry를 먼저 보여 준다.

request handler는 `TReply`를 바로 반환하거나 `task_t<TReply>`를 반환할 수 있다.
후자는 `.NET`의 `async Task<TReply>` handler와 같은 의미다. handler 안에서 다른
request/relay를 기다려야 하면 blocking wait를 쓰지 않고 `co_await call.async()`를
사용한다.

CPU-bound 또는 blocking 가능성이 있는 handler는 framework handler coroutine executor에서
실행한다. `options.handlers().add<THandler>(...)`는 기본적으로 offload 실행 정책을 적용한다.
사용자가 host factory에서 handler마다 실행 정책을 반복해서 쓰게 만들지 않는다.

```cpp
options.handlers()
  .add<match_handler_t>("match-api");
```

## 4. Messaging 주입 기준

handler나 service가 outbound messaging을 해야 하면 framework가 기본 등록한 messaging
service를 DI로 받는다.

```cpp
class order_service_t final {
public:
    explicit order_service_t(zlink::framework::request_client_t &client)
      : client_(client)
    {
    }

    zlink::framework::request_call_t<order_status_reply_t> get_status(
      order_status_query_t query)
    {
        return client_
          .request<order_status_reply_t>("orders", query)
          .timeout(std::chrono::seconds(2));
    }

private:
    zlink::framework::request_client_t &client_;
};
```

event publish는 channel name과 topic을 함께 받는다.

```cpp
publisher.publish(
  "orders",
  "orders.created",
  event,
  zlink::framework::send_options_t{
    .packet_name = "orders.created",
  });
```

## 5. Host 구성 기준

runtime 구성은 `add_zlink_framework(...)` 하나로 들어간다. channel 연결 설정은 core
capability builder를 직접 노출하지 않고, framework options의 channel builder가 필요한
부분만 받는다.

```cpp
app.add_zlink_framework([](auto &options) {
    options.use_discovery().add_registry_endpoint ("tcp://registry:5551");
    options.add_client_server_channel("orders")
      .enable_server("tcp://0.0.0.0:7001")
      .enable_client()
      .use_handler_group("orders-api");
    options.add_fanout_channel("orders.events")
      .enable_publisher("tcp://0.0.0.0:7002");
});
```

수동 연결은 capability 안에서 endpoint 기준으로 설정한다. 같은 capability 안에서
수동 연결과 Discovery 연결을 섞지 않는다.

handler group은 channel에 노출되는 handler packet 집합이다. 같은 channel과 같은
packet 이름에 대해 request, send, publish handler가 중복 노출되면 framework options
구성 중 `request_protocol_error`로 실패한다. group을 channel에 먼저 연결한 뒤 handler를
등록하든, handler를 먼저 등록한 뒤 group을 channel에 연결하든 같은 규칙을 적용한다.

## 6. SPOT 기준

`SPOT`은 binding의 `zlink::service::spot_node_t`와 `zlink::service::spot_t`를
framework builder와 `spot_context_t`로 감싸서 제공한다.

```cpp
app.add_zlink_framework([](auto &options) {
    options.add_fanout_channel("game.stage")
      .enable_publisher("tcp://0.0.0.0:7001");
    options.add_client_server_channel("profile")
      .enable_client();
    options.add_spot_mesh("game.stage")
      .add_node("stage-spot-node")
      .enable_pub_sub("tcp://0.0.0.0:9000")
      .enable_actor_gateway()
      .attach_channel_client("profile")
      .attach_publisher("game.stage")
      .add_entry_spot<player_entry_spot_t>()
      .add_actor_factory<player_actor_factory_t>("player")
      .add_spot<stage_spot_t>("stage");
});
```

직접 `routing_id_t`를 받는 API는 spot-to-spot send/request와 Entry Spot join 경로에
제한한다. 일반 application handler와 publisher는 channel name과 topic을 먼저 사용한다.
current Spot 밖에서 target Spot을 직접 호출하는 별도 public client는 기본 표면에 두지
않는다.

attached channel client는 registry discovery 또는 attach별 manual endpoint로 peer를 얻는다.
manual endpoint가 필요한 경우 `attach_channel_client(name, [](auto &client) {
client.connect(endpoint); })`처럼 attach 설정 안에서 지정한다.

SPOT timer는 CAPI timer 등록을 감싼 framework timer handle과 `timer_tick_t` metadata로
설명한다. user Spot timer는 CAPI SPOT dispatch event 후 recv 경계에서 순서 정책을 따르고,
Entry Spot timer는 Entry Spot 전체를 전역 직렬화하지 않는다. C++ framework는 CAPI timer를
사용하므로 timer callback 실행 직렬화를 위한 별도 queue나 자체 timer scheduler를 만들지 않는다.

Session actor relay는 application route mesh channel을 쓰지 않는다. STREAM session은
`attach_actor_gateway(...)`로 local SpotNode에 붙고, packet relay는
`session_actor_t::relay(...)`로 표현한다.

## 7. 중요한 규칙

- `C++` framework 문서는 공통 framework 정책과 C++ binding public spec을 함께 따른다.
- 구현 전 설계는 이 디렉토리의 draft 문서에만 둔다.
- handler public contract는 `contracts/handlers/*`가 소유하고, handler descriptor map,
  DI resolve, serializer 호출 순서, dispatch lookup 구현은 `src/runtime/handlers/*`에 둔다.
- handler filter는 `handler_invocation_context_t`로 descriptor, dispatch context, immutable
  message payload를 읽을 수 있다. filter가 payload를 바꾸려면 `next()` 결과 대신 새
  `message_t`를 반환한다.
- handler template 코드는 handler shape 검사와 type-erased runtime 호출로 제한한다.
  pending queue, recv loop, monitoring event 생성 구현을 `contracts/detail/*`에 넣지 않는다.
- public surface는 native socket, poller, callback userdata를 직접 노출하지 않는다.
- 같은 capability는 자동 연결과 수동 연결 중 하나만 선택한다.
- 일반 channel messaging의 handler dispatch는 local server capability ingress 기준이다.
- outbound client capability의 receive path는 reply correlation 경로로 본다.
- `ROUTER -> DEALER` 임의 push는 channel messaging 공용 계약에 넣지 않는다.
- session actor relay는 ActorGateway attach와 logical actor handle 기준으로 설명한다.
- Registry는 Spot remote address 조회 기본값으로 쓰고 actor-session binding 저장소로
  쓰지 않는다.
