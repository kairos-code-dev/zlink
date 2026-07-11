<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../README.ko.md) | [이전: Spec -- ZLink Framework C++ STREAM](cpp-stream.ko.md) | [다음: C++ SPOT Samples](../../../../cpp/guide/samples/spot-samples.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[C++ 묶음](../../../../cpp/README.ko.md) | [Runtime Architecture](../../../../cpp/internals/runtime-architecture.ko.md) | [Framework 인터페이스](cpp-framework-interfaces.ko.md) | [channel](cpp-channel-messaging.ko.md) | [SPOT](cpp-spot.ko.md) | [STREAM](cpp-stream.ko.md) | [Monitoring](cpp-monitoring.ko.md) | [Registry](cpp-registry.ko.md)

# Spec -- ZLink Framework C++ Public Interface

> 이 문서는 C++ framework의 정식 public interface 계약이다.
> 공통 기능을 C++의 값 타입, RAII 객체, coroutine awaitable로 표현하는 정확한
> 시그니처와 현재 구현의 차이를 함께 기록한다. 전체 public interface의 구성은
> [Framework 인터페이스](cpp-framework-interfaces.ko.md)다.

## 1. 역할

이 문서는 낮은 수준의 handler class와 client surface를 framework public contract로
정리할 때 지켜야 할 기준을 정의한다.

관련 public interface와 실행 구조는 아래 문서에서 설명한다.

- [Runtime Architecture](../../../../cpp/internals/runtime-architecture.ko.md)
- [Framework 인터페이스](cpp-framework-interfaces.ko.md)

이 문서의 선언은 정식 계약이다. 현재 public header와 다른 선언은 §8.1에서
구현 차이로 명시하며, 구현은 그 차이를 해소할 때 이 문서의 선언을 따른다.

## 2. 공개 계약 기준

낮은 수준 표면은 아래 기준으로 정리한다.

| 낮은 수준 표현 | framework 계약 |
|----------------|-------------------|
| `app_t` builder chain 직접 조립 | `app_t::create()` 후 `add_zlink_framework(...)`의 options builder로 구성 |
| raw request/send/event handler class | application handler는 `handler_registry_t`의 typed handler 등록, SPOT handler는 `spot_context_t::handlers()`의 Spot member function 등록 |
| channel client 직접 주입 | `message_bus_t`, `request_client_t`, `publisher_t` DI 주입 |
| event publisher 전용 타입 | `publisher_t::publish(channel, topic, event)` |
| channel 전체 연결 설정 | 역할 builder의 `bind`, `connect`, 인자 없는 `enable_client`/`enable_subscriber` |
| spot 전용 publisher client | `spot_context_t` 또는 `spot_publisher_client_t::publish(channel, topic, event)` |
| target Spot 직접 호출 public client | actor 생성 또는 Entry Spot join 뒤 actor/session handle 사용 |
| raw timer callback | `spot_context_t::add_timer(...)`와 `timer_tick_t` metadata |

application handler owner는 `options.handlers().group(...).add<THandler>()`로 등록한 타입이어야 한다.
생성자 주입이 필요하면 handler 타입의 `dependency_types`에 의존 타입을 적는다. 이 규칙은
handler lifecycle과 shutdown 중 resolve 금지 같은 host 정책을 한곳에서 닫기 위해 필요하다.

SPOT handler는 별도 handler class로 등록하지 않는다. `spot_context_t::handlers()`는
request는 `add_actor_request<&room_spot_t::place_mark>()`, one-way send는
`add_actor_send<&room_spot_t::notify>()`처럼 Spot 객체의 member function을 받는다.
Spot은 상태와 동작을 함께 감싸는 단위이므로 handler class를 따로 만들면 상태 owner와
동작 owner가 분리되어 샘플과 실제 구현이 달라진다.
일반 Spot은 `zlink::framework::spot_t`, Entry Spot은 `zlink::framework::entry_spot_t`를
상속한다. framework는 타입 이름에서 Entry Spot 여부를 추론하지 않고
`add_entry_spot<TEntrySpot>()` 호출과 기반 타입으로 역할을 확인한다.

actor lifecycle은 handler registry 등록 대상이 아니다. user Spot의 join admission은
`on_actor_join(std::string_view actor_id, zlink::framework::message_t)` member callback이 처리한다.
admission에는 actor id만 넘기고 actor instance는 없다. 반환값은 accepted 여부와
optional reply `zlink::framework::message_t`를 담는다. accepted가 `true`일 때만 actor 위치를 user Spot으로
commit하고 `on_actor_joined(actor)`를 호출한다. accepted가 `false`이면 위치를 바꾸지
않고 post-joined callback도 호출하지 않는다. Entry Spot도 명시적 재진입
(`join_entry_spot(node_rid, request)`)에 대해 같은 `on_actor_join(actor_id, zlink::framework::message_t)`
admission을 선택적으로 둘 수 있고, 선언하지 않으면 재진입은 그대로 accept된다. actor
최초 생성 직후 첫 Entry Spot 배치는 admission이 아니라
`on_create_actor(actor, createRequest)`로 처리하며,
commit 이후 `on_actor_joined(actor)`와 `on_leave_actor(actor)`를 둔다.
payload가 필요 없는 Entry Spot은 `on_create_actor(actor)` overload를 선언할 수 있다.

remote transfer에서 상태를 옮길 actor type은 Spot node 구성에서
`add_actor_transfer_adapter<TActor, TAdapter>(actorType)`을 사용한다. 등록되지 않은 actor type의
remote transfer는 실패가 아니라 framework 기본 빈 state transfer로 처리한다. 이 기본 경로는 source에서
빈 `message_t`를 보내고 target에서 actor factory 또는 public actor 생성 경로로 actor를 만든다.

create callback도 request를 단일 `zlink::framework::message_t`로 받는다. create result는 `existing`,
`created`, `rejected` state와 optional reply `zlink::framework::message_t`를 담는다. `spot_context_t::close()`는
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
    options.add_client_server_channel("orders")
      .enable_server("tcp://0.0.0.0:7001")
      .use_handler_group("orders-api");
    options.handlers()
      .group ("orders-api")
      .add<order_created_handler_t> ()
      .add<get_order_status_handler_t> ();
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
실행한다. `options.handlers().group(...).add<THandler>()`는 기본적으로 offload 실행 정책을 적용한다.
사용자가 host factory에서 handler마다 실행 정책을 반복해서 쓰게 만들지 않는다.

```cpp
options.handlers()
  .group ("match-api")
  .add<match_handler_t> ();
```

## 4. Messaging 주입 기준

handler나 service가 outbound messaging을 해야 하면 framework가 기본 등록한 messaging
service를 DI로 받는다.

```cpp
class order_service_t final {
public:
    explicit order_service_t(zlink::framework::channel_client_t &client)
      : client_(client)
    {
    }

    zlink::framework::channel_yield_request_call_t get_status(
      order_status_query_t query)
    {
        return client_
          .request("orders", query)
          .timeout(std::chrono::seconds(2));
    }

private:
    zlink::framework::channel_client_t &client_;
};
```

event publish는 channel name과 topic을 함께 받는다.

```cpp
publisher.publish("orders", "orders.created", event)
  .packet_name("orders.created");
```

## 5. Host 구성 기준

runtime 구성은 `add_zlink_framework(...)` 하나로 들어간다. channel 연결 설정은 core
역할 builder를 직접 노출하지 않고, framework options의 channel builder가 필요한
부분만 받는다.

```cpp
app.add_zlink_framework([](auto &options) {
    options.add_client_server_channel("orders")
      .enable_server("tcp://0.0.0.0:7001")
      .enable_client()
      .use_handler_group("orders-api");
    options.add_fanout_channel("orders.events")
      .enable_publisher("tcp://0.0.0.0:7002");
});
```

수동 연결은 역할 안에서 endpoint 기준으로 설정한다. 같은 역할 안에서
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
      .enable_pub_sub("tcp://0.0.0.0:9000")
      .add_entry_spot<player_entry_spot_t>()
      .add_actor_factory<player_actor_factory_t>("player")
      .add_spot<stage_spot_t>("stage");
});
```

직접 `routing_id_t`를 받는 API는 spot-to-spot send/request와 Entry Spot join 경로에
제한한다. 일반 application handler와 publisher는 channel name과 topic을 먼저 사용한다.
current Spot 밖에서 target Spot을 직접 호출하는 별도 public client는 기본 표면에 두지
않는다.

SPOT handler가 client/server channel로 send/request 하려면 해당 channel에서
`enable_client(...)`를 설정한다.

SPOT timer는 CAPI timer 등록을 감싼 framework timer handle과 `timer_tick_t` metadata로
설명한다. user Spot timer는 CAPI SPOT dispatch event 후 recv 경계에서 순서 정책을 따르고,
Entry Spot timer는 Entry Spot lifecycle callback, request continuation과 같은 Entry Spot
실행 줄에서 처리한다. Entry Spot actor packet은 대상 actor mailbox에서 처리한다. C++
framework는 CAPI timer를 감싸되 application callback 실행 순서는 Spot runtime의 직렬 실행
큐가 정한다.

Session actor relay는 application route mesh channel을 쓰지 않는다. STREAM session은
`session_actor_t::relay(...)`로 표현한다.

## 7. 중요한 규칙

- `C++` framework 문서는 공통 framework 정책과 C++ binding public spec을 함께 따른다.
- 이 문서의 목표 interface와 현재 public header가 다르면 아래 구현 차이 표에 기록하고
  구현을 정식 계약에 맞춘다.
- handler public contract는 `contracts/handlers/*`가 소유하고, handler descriptor map,
  DI resolve, serializer 호출 순서, dispatch lookup 구현은 `src/runtime/handlers/*`에 둔다.
- handler filter는 `handler_invocation_context_t`로 descriptor, dispatch context, immutable
  message payload를 읽을 수 있다. filter가 payload를 바꾸려면 `next()` 결과 대신 새
  `message_t`를 반환한다.
- handler template 코드는 handler shape 검사와 type-erased runtime 호출로 제한한다.
  pending queue, recv loop, monitoring event 생성 구현을 `contracts/detail/*`에 넣지 않는다.
- public surface는 native socket, poller, callback userdata를 직접 노출하지 않는다.
- 같은 역할은 자동 연결과 수동 연결 중 하나만 선택한다.
- 일반 channel messaging의 handler dispatch는 local server 역할 ingress 기준이다.
- outbound client 역할의 receive path는 reply correlation 경로로 본다.
- `ROUTER -> DEALER` 임의 push는 channel messaging 공용 계약에 넣지 않는다.
- session actor relay는 session relay와 logical actor handle 기준으로 설명한다.
- Registry는 Spot remote address 조회 기본값으로 쓰고 actor-session binding 저장소로
  쓰지 않는다.

## 8. Public interface와 구현 차이

C++는 모든 역할을 pure virtual interface로 표현하지 않는다. 사용자 확장점은 abstract
interface 또는 template 요구 조건으로, 호출 표면은 RAII facade와 값 타입으로 제공한다.
아래 표는 목표 선언을 고정한다. 현재 header가 다른 부분은 §8.1에 모두 연결한다.
`detail` namespace의 state와 runtime owner는 공개 계약에 포함하지 않는다.

| 기능 | 목표 C++ 선언 |
|------|---------------|
| handler context | `struct handler_context_t { std::string channel_name; std::string packet_name; std::string content_type; };` `request_context_t`와 `send_context_t`는 이를 상속한다. `publish_context_t`는 `std::string topic`과 `std::string source`를 추가한다. |
| handler filter | `struct handler_invocation_context_t { handler_descriptor_t descriptor; handler_context_t context; std::shared_ptr<const zlink::message_t> message; };` `using handler_next_t = std::function<task_t<zlink::message_t>()>;` |
| handler registry | `on_request`는 `std::string channel_name`, `std::string topic`, owner member function, `handler_options_t`를 받는다. member function은 `TReply` 또는 `task_t<TReply>`를 반환하며 선택적으로 `const request_context_t &`를 받는다. `on_send`와 `on_event`도 각각 `send_context_t`, `publish_context_t`를 선택적으로 받는다. filter는 `use_filter<TFilter>()`로 등록한다. |
| channel call | `send_call_t &packet_name(std::string); send_call_t &metadata(std::string, std::string); void submit();` `channel_request_call_t`는 같은 설정과 `timeout(std::chrono::milliseconds)`, `task_t<TReply> async<TReply>()`를 제공한다. `channel_yield_request_call_t`는 `task_t<TReply> yield<TReply>()`를 추가한다. |
| channel client | `send_call_t channel_client_t::send(std::string channel, TMessage message);` `channel_yield_request_call_t channel_client_t::request(std::string channel, TRequest request);` |
| route client | `route_send_call_t route_client_t::send_to_node(std::string channel, zlink::routing_id_t target, TMessage message);` `channel_request_call_t route_client_t::request_to_node(std::string channel, zlink::routing_id_t target, TRequest request);` send는 `submit()`, request는 `async<TReply>()`로 끝낸다. |
| publisher | `send_call_t publisher_t::publish(std::string channel_name, std::string topic, TEvent event);` 반환 call은 `packet_name(std::string)`과 목표 종결자 `void submit()`을 제공한다. |
| actor value/client | `actor_ref_t(node_rid_t node_rid, std::string actor_type, std::string actor_id, std::uint64_t generation = 1);`와 `node_rid()`, `actor_type()`, `actor_id()`, `generation()`, `empty()`를 제공한다. `actor_client_t::send_to_actor(actor_ref_t, TMessage)`는 `actor_send_call_t`, `request_to_actor(actor_ref_t, TRequest)`는 `actor_request_call_t`를 반환한다. 목표 actor send call은 `packet_name(std::string)`과 `void submit()`을 제공한다. |
| actor directory | `task_t<std::optional<actor_ref_t>> find(std::string actor_id);` `task_t<actor_ref_t> ensure(std::string actor_id, message_t create_request, actor_placement_t placement = {});` |
| actor context | `const actor_ref_t &actor_ref() const noexcept; bool is_joined() const noexcept; bound_session_t bound_session() const;`와 `actor_yield_join_call_t join_spot(spot_rid_t, ...)`, `actor_yield_join_call_t join_entry_spot(node_rid_t, ...)`를 제공한다. 두 join은 `message_t`와 typed request overload를 제공한다. |
| actor join call | `actor_join_call_t &timeout(std::chrono::milliseconds); task_t<actor_join_result_t> async(); task_t<typed_actor_join_result_t<TReply>> async<TReply>();` `actor_yield_join_call_t`는 같은 두 형태의 `yield()`를 추가한다. |
| actor factory/transfer | actor factory는 builder의 typed factory 요구 조건을 만족한다. `actor_transfer_adapter_t<TActor>`는 `task_t<message_t> transfer_out(const TActor &actor)`와 `task_t<TActor> transfer_in(std::string actor_id, message_t state)`를 제공하며 중단 토큰을 받지 않는다. |
| Spot lifecycle | `spot_t`와 `entry_spot_t`는 역할을 표시하는 기반 타입이다. 목표 lifecycle callback은 `on_actor_join`, `on_actor_joined`, `on_create_actor`, `on_leave_actor`, `on_disconnect_actor`의 snake_case 이름을 사용하고 비동기 완료를 `task_t<T>`로 표현한다. 현재 구현의 duck typing 이름과 동기 반환 차이는 §8.1에 기록한다. |
| Spot context/registry | `spot_context_t`는 `node_rid_t node_rid() const`, `spot_rid_t spot_rid() const`, `std::string spot_name() const`, `spot_handler_registry_t handlers()`, `spot_node_manager_t manager() const`, `channel_client_t outbound() const`, `task_t<bool> close()`를 제공한다. actor 이탈은 `task_t<actor_ref_t> leave_actor(const actor_ref_t &, TActor &)`다. timer 등록은 `timer_t add_timer<THandler>(std::string, std::chrono::milliseconds, timer_options_t = {})`다. registry는 `add_handler<Method>`, `add_subscribe<Method>`, `add_actor_send<Method>`, `add_actor_request<Method>`를 제공한다. |
| Spot manager/publisher | `spot_node_manager_t`는 `spot_create_result_t create_spot(std::string, ...)`, `spot_create_result_t get_or_create_spot(std::string, spot_rid_t, ...)`, `std::optional<spot_info_t> find_spot(spot_rid_t) const`, `task_t<bool> close_spot(spot_rid_t)`를 제공한다. `spot_publisher_client_t::publish(std::string channel_name, std::string topic, const TEvent &) const`는 `task_t<void>`를 반환한다. |
| stream | `stream_t`는 `std::string session_id() const`, `task_t<void> close()`, `stream_write_call_t write_packet(const zlink::message_t &)`, `reply_packet(const zlink::message_t &)`를 제공한다. write call은 metadata, packet name, compression 설정과 목표 종결자 `void submit()`을 제공한다. |
| typed stream session | `packet_stream_session_t`는 `on_connected(stream_t &)`, `on_disconnected(stream_t &)`, `on_error(stream_t &, const stream_error_t &)`와 `on_packet(stream_t &, const stream_dispatch_context_t &, const zlink::message_t &)`를 `task_t<void>`로 반환한다. 목표 typed handler concept는 `task_t<void> handle(TSessionContext &, const TPayload &)`를 만족한다. |
| session actor/bound session | `session_actor_t::relay(const zlink::message_t &)`와 `relay(std::string packet_name, const zlink::message_t &)`는 `relay_call_t`를 반환한다. `bound_session_t::send(const TMessage &)`와 `bound_session_t::disconnect()`는 `bound_session_send_call_t`를 반환한다. 목표 one-way call 종결자는 `void submit()`이다. |
| location store | `location_store_t`는 역할별 store를 상속하고 `task_t<std::int64_t> remove_all_by_owner(std::string owner_id)`를 제공한다. update/remove/resolve/list 인자는 public header와 같이 값으로 받는다. peer 목록은 `task_t<std::vector<peer_location_t>> list_peers(peer_location_filter_t)`이고 Spot/actor/route 목록은 `task_t<location_page_t<T>> list_*(filter, location_page_request_t page = {})`다. |
| lease/watch/stamp | `owner_lease_store_t`는 `renew_owner_lease`, `remove_owner_lease`, `list_owner_leases`를 제공한다. `using location_watch_callback_t = std::function<void(location_changed_t)>;`이며 `task_t<void> location_watch_store_t::watch_locations(location_watch_filter_t filter, location_watch_callback_t callback)`로 변경을 전달한다. `get_change_stamp(location_change_stamp_scope_t)`는 `task_t<std::int64_t>`를 반환한다. |
| resolver/readiness/query | `peer_location_resolver_t::list_live_peers(filter)`와 `spot_location_resolver_t::resolve_spot_ref(mesh, spot_rid)`를 제공한다. `location_readiness_t::is_peer_ready(mesh, role, optional node_rid)`는 `task_t<bool>`을 반환한다. `location_runtime_query_t`는 status, peer/Spot/actor/route location, topology와 service summary 조회를 모두 `task_t<T>`로 제공한다. |
| codec/compression | `serializer_registry_t::add<T>(serializer_t<T>::serialize_fn_t, serializer_t<T>::deserialize_fn_t, std::string content_type)`와 `get<T>()`를 제공한다. `stream_compression_codec_t`는 `compress(const zlink::message_t &) const`와 `decompress(const zlink::message_t &, std::size_t max_decompressed_size) const`를 제공한다. `stream_compression_options_builder_t`는 `use(std::shared_ptr<const stream_compression_codec_t>)`와 `disable()`을 제공한다. |
| configuration | `zlink_framework_options_t`는 `handlers()`, channel/route-mesh/fanout/stream/Spot builder 생성, location store 등록, dispatch/worker 설정을 제공한다. 각 builder는 역할에 맞는 `bind`, `connect`, discovery를 선택하는 인자 없는 enable 함수와 `use_handler_group`만 노출한다. |
| dispatch/monitoring | `dispatch_options_t::message_flow(message_flow_log_mode_t)`가 초기 mode를 설정한다. 실행 중 제어는 `app_t &set_message_flow_mode(message_flow_log_mode_t) noexcept`와 `message_flow_log_mode_t message_flow_mode() const noexcept`가 제공한다. 별도 `message_flow_control_t` interface를 만들지 않는다. |
| timer/worker | `timer_t`는 `bool is_disposed() const noexcept`와 `void cancel() noexcept`를 제공한다. `worker_call_t<TResult>`는 `timeout(...)`, `task_t<TResult> async()`, `task_t<TResult> yield()`, `void submit(completion_callback_t)`를 제공한다. `completion_callback_t`는 `std::function<task_t<void>(result_t<TResult>)>`다. callback에 중단 토큰을 전달하지 않는다. |
| host/DI | `app_t::create()`, `add_zlink_framework(...)`, `run`, `request_stop`을 제공한다. `service_provider_t`는 `get_required<T>()`, `get<T>()`; `service_collection_t`는 singleton/scoped/transient 등록; `service_scope_t`는 scoped provider 조회를 제공한다. |

`task_t<T>`는 C++ coroutine awaitable이다. 목표 계약은 framework 전용
`cancellation_token_t`와 `cancellation_token_source_t`를 포함하지 않는다. 현재 배포된
header에는 두 타입과 request/yield overload가 남아 있으며, 이는 아래 표의 구현 차이다.
장기 작업 중단이 필요하면 C++ 관례에 맞는 타입과 적용 범위를 정식 계약 변경으로 먼저 명시한다.

### 8.1 목표 interface와 현재 구현

| 기능 | 목표 C++ 계약 | 현재 구현 | 상태와 구현 참고 |
|------|---------------|-----------|------------------|
| coroutine handler | handler와 lifecycle의 `task_t<T>` 완료를 executor가 재개 | `spot_node_builder_t::add_actor_transfer_adapter`가 만든 `transfer_out`/`transfer_in` invoker가 `task_t<T>::result()`를 호출 | gap. 두 `result()` bridge를 제거하고 executor의 coroutine chain에서 `co_await`한다. |
| one-way call | `send_call_t`, `route_send_call_t`, `actor_send_call_t`, `stream_write_call_t`, `bound_session_send_call_t`의 `submit(): void` | `send_call_t`, `route_send_call_t`, `stream_write_call_t`, `bound_session_send_call_t`는 `result_t<void> submit()`을 제공하고, `actor_send_call_t`는 `task_t<void> async()`를 제공한다. `relay_call_t::submit()`은 이미 `void`다. | gap. 네 `result_t<void>` 종결자와 actor의 `async()`를 `void submit()`으로 맞춘다. request 계열만 완료값을 반환한다. |
| request/yield | `channel_request_call_t::async<TReply>()`, `channel_yield_request_call_t::yield<TReply>()`, `actor_yield_join_call_t::yield()`가 `task_t<T>`를 반환 | 해당 call 타입과 멤버가 공개 header에 존재한다. | 일치. 다만 token을 받는 yield overload는 아래 cancellation 차이로 분리한다. |
| Spot lifecycle | `on_actor_join`, `on_actor_joined`, `on_create_actor`, `on_leave_actor`, `on_disconnect_actor`를 snake_case와 `task_t<T>` 완료로 제공 | detection은 `on_actor_join`, `on_actor_joined`, `onCreateActor`, `onLeaveActor`, `onDisconnectActor`를 탐색한다. join 이후 callback은 선택 사항이고 callback 반환도 동기 형식이다. | gap. camelCase 세 이름을 snake_case로 바꾸고 lifecycle callback의 비동기 완료와 필수 여부를 target concept에 고정한다. |
| transfer adapter | `transfer_out`/`transfer_in`이 signal 인자 없는 `task_t<T>` 반환 | 등록 adapter invoker가 두 task에 `.result()` 호출 | gap. 두 invoker를 coroutine으로 만들고 lifecycle task chain에서 `co_await`한다. |
| cancellation | framework 전용 token을 노출하지 않는다. 필요한 장기 작업만 정식 spec에 고정된 C++ 중단 관례를 사용한다. | `cancellation_token_t`, `cancellation_token_source_t`, `request_call_t<TReply>::yield(cancellation_token_t)`, `channel_yield_request_call_t::yield<TReply>(cancellation_token_t)`가 공개 header에 존재한다. | gap. 두 token 타입과 두 yield overload를 public contract에서 제거한다. 대체 중단 API는 별도 계약이 승인되기 전에는 추가하지 않는다. |
| typed session handler | `typed_session_packet_handler_for<T, TContext, TPayload>`의 `handle(TContext &, const TPayload &): task_t<void>` | `packet_stream_session_t::on_packet(...)`의 raw `message_t` callback만 존재 | gap. serializer registry 이후 typed invoker를 추가하고 raw callback은 session runtime 경계에만 둔다. |
| location watch callback | `watch_locations(location_watch_filter_t, location_watch_callback_t): task_t<void>` | 목표 선언과 callback 값 전달 형식이 공개 header에 존재한다. | 일치. 별도 `async_range_t`나 `watch()`를 추가하지 않는다. |
| message-flow runtime control | `dispatch_options_t::message_flow(...)`와 `app_t::set_message_flow_mode(...)`/`message_flow_mode()` | 세 멤버가 공개 header에 존재한다. | 일치. 별도 control interface를 추가하지 않는다. |
| monitoring 자동 event | 등록된 channel, registry, Spot과 timer failure를 typed event로 자동 발행 | source 등록과 직접 publisher는 있으나 runtime 자동 발행 연결이 E2E로 검증되지 않음 | gap. 네 source family를 publisher path에 연결하고 E2E에서 payload를 관측한다. |
| route-mesh runtime options | `channel_runtime_options_t::client_server_channel(name)`와 `route_mesh_channel(name)` | `client_server_channel(name)`만 공개 | gap. `route_mesh_channel(std::string_view)`와 `route_mesh_channel_runtime_options_t`를 추가한다. |
| error kind | 공통 오류 집합만 public | `framework_error_kind_t`가 `actor_stale_generation`, `timeout`, `shutdown`, `disconnected`, `closed`, `cancelled`를 공개 | gap. 공통 오류로 승인되지 않은 여섯 enumerator를 public enum에서 제거하고 내부 상태로 옮긴다. |
| public/runtime 경계 | facade, 값 타입과 extension contract만 application namespace에 둔다. | `actor_gateway_state_t`, `timer_state_t`, `service_scope_state_t`, `zlink_builder_state_t`, `app_state_t`, `serializer_registry_state_t`, `handler_registry_state_t`, `stream_state_t`는 모두 `detail` namespace에 선언되어 있다. | 일치. 이 state들을 application public contract로 문서화하지 않는다. |

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../../README.ko.md) | [이전: Spec -- ZLink Framework C++ STREAM](cpp-stream.ko.md) | [다음: C++ SPOT Samples](../../../../cpp/guide/samples/spot-samples.ko.md)
<!-- framework-adapter-nav:bottom:end -->
