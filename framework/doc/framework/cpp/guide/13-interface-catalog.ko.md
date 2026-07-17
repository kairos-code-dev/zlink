[← 목차](README.ko.md)

# 13. 인터페이스 카탈로그

자주 쓰는 표면을 도메인별로 모은 레퍼런스다. 사용법·예제는 각 장이 다루고, 여기서는
**실재하는 계약 표면**만 나열한다. 헤더 기준 경로는
`framework/languages/cpp/framework/include/zlink/framework/contracts/` 아래다.

## 0. 확장 지점 — 사용자가 구현하는 것

| 확장 지점 | 형태 | 장 |
|-----------|------|-----|
| 채널/HTTP 핸들러 | `request_type`/`reply_type`/`topic_name` + `handle()` (동기 또는 `task_t`) | [3](03-concepts.ko.md)·[7](07-channel-messaging.ko.md)·[6](06-http-hosting.ko.md) |
| 핸들러 DI | `using dependency_types = dependency_list_t<...>` + 생성자 주입 | [3 §6.1](03-concepts.ko.md) |
| handler filter | `invoke(const handler_invocation_context_t &, handler_next_t)` (duck-typed) | [7 §4](07-channel-messaging.ko.md) |
| spot | `spot_t`/`entry_spot_t` 상속 + `configure(spot_context_t&)` | [8](08-spot.ko.md) |
| spot actor 패킷 핸들러 | `(TActor&, spot_actor_request_context_t&\|spot_actor_send_context_t&, const req_t&) → res_t` | [8 §3](08-spot.ko.md) |
| actor + factory | actor struct + factory `create(actor_id)` (duck-typed) | [9 §2](09-actor-session.ko.md) |
| stream session | `packet_stream_session_t` 상속 — `on_connected/packet/error/disconnected` | [10 §3](10-stream.ko.md) |
| timer | `context.add_timer<THandler>(name, period, options)` — THandler 는 진단용 타입 태그(handle 자동 호출 아님) | [8 §5](08-spot.ko.md) |
| HTTP middleware | `use<TMiddleware>()` — before/after 훅 | [6 §4](06-http-hosting.ko.md) |
| hosted service | `hosted_service_t` — `start(provider)`/`stop() noexcept` | [3 §6.3](03-concepts.ko.md) |
| module | `module_t` — `configure_services/zlink/handlers/monitoring` | [3 §6.4](03-concepts.ko.md) |
| 설정 바인딩 | `static T bind(const configuration_section_t&)` | [5 §5](05-configuration.ko.md) |
| 메시지(packet) | struct + `static constexpr const char *packet_name` (+codec ADL) | [2 §2](02-getting-started.ko.md) |

## 1. Channel messaging — outbound client · handler · codec

**outbound 호출 표면** (`channels/channel.hpp`):

| 타입 | 핵심 호출 | 획득 |
|------|----------|------|
| `request_client_t` | `request(mesh, channel, req)` → `channel_request_call_t`; `send(mesh, channel, msg)` → `send_call_t` | DI 주입 또는 `message_bus_t::client()` |
| `publisher_t` | `publish(channel, topic, event)` → `void` | DI 주입 또는 `message_bus_t::publisher()` |
| `message_bus_t` | `client()`과 `publisher()`로 두 facade를 제공 | DI 주입 |
| `route_client_t` | Node·ChannelName·Spot 대상 send는 `route_send_call_t`, request는 `channel_request_call_t` | DI 주입 |

**종결 호출(call) 객체** (`channels/call.hpp`)는 전송 전에 timeout과 metadata를
모은다. `channel_request_call_t`는 `.timeout()` · `.metadata(k,v)` ·
`.async<TReply>()` · `.yield<TReply>()`를 제공한다. `send_call_t`와
`route_send_call_t`는 `.timeout()` · `.metadata(k,v)` · `.try_submit()` ·
`.submit()`을 제공하고 `submit_result_t`로 admission 결과를 반환한다. Route handler는
`mesh_node_builder_t::add_route_send_handler(...)`와
`add_route_request_handler(...)`로 등록한다.

**handler/filter 계약** (`handlers/handler_registry.hpp`):

| 항목 | 표면 |
|------|------|
| handler 종류 | `handler_kind_t{request, send, event, raw}` |
| handler context | `request_context_t` · `send_context_t` · `publish_context_t`(topic/source) |
| filter 계약 | `TFilter::invoke(const handler_invocation_context_t&, handler_next_t)` — `next()` 호출 안 하면 handler 미실행 |
| `handler_invocation_context_t` | `{descriptor, context, shared_ptr<const message_t> message}` |
| 등록 | `options.handlers().group("group").add<T>()` / `use_filter<TFilter>()` |

**codec 등록** (`codecs/serializer.hpp`): `serializer_registry_t` — JSON 기본 serializer는
별도 JSON 등록 없이 사용한다. Protobuf와 MessagePack은 framework codec extension package를
참조한 뒤 `codecs().use(extension)`으로 등록한다. custom codec도 같은 extension 객체 안에서
`add_serializer<T>(serialize_fn, deserialize_fn)`를 호출해 등록한다.

Node direct route handler는 `mesh_node_builder_t::add_route_send_handler(...)`와
`add_route_request_handler(...)`로 등록한다.

## 2. Configuration — app · options · builder · module

**`app_t`** (`configuration/app.hpp`):

| 메서드 | 역할 |
|--------|------|
| `create()` | 앱 생성 |
| `config()` / `logging()` / `monitoring()` / `metrics()` / `health()` | 부속 구성 |
| `add_zlink_framework(람다)` / `add_zlink_framework<TModule>(...)` | zlink 토폴로지 선언 |
| `add_module(module)` / `add_hosted_service(service)` | 패키징 / 백그라운드 |
| `run(argc, argv)` | 실행 (블로킹, 종료 코드 반환) |
| `stop()` / `request_stop()` | 종료 (동기 / 비동기 요청) |
| `advanced()` | `services()` / `handlers()` / `zlink()` 직접 접근 |

**`zlink_framework_options_t`** (`configuration/framework_options.hpp`):

| 진입점 | 내용 | 장 |
|--------|------|-----|
| `services()` | DI 등록 (`add_singleton/scoped/transient<T>`) | [4장](04-di-container.ko.md) |
| `handlers()` | 핸들러 그룹 등록 (`group("group").add<T>()`) | [3 §6.1](03-concepts.ko.md) |
| `codecs()` | `use(extension)` | [7 §2](07-channel-messaging.ko.md) |
| `add_fanout_channel(name)` | `enable_publisher(ep)` · `enable_subscriber([ep])` · `use_handler_group(g)` | [7 §6](07-channel-messaging.ko.md) |
| `add_route_mesh(name)` | `listen(ep)` · `set_routing_id(rid)` · `channel_name(name)` · `peer_connections()` · Spot/Actor 등록 | [7 §7](07-channel-messaging.ko.md)·[8](08-spot.ko.md) |
| `add_stream_node(name)` | `bind(ep)` · `set_tls_server(...)` · `enable_actor_dispatch(mesh)` · `register_session<T>()` | [10](10-stream.ko.md) |
| `add_location_store(store)` / `configure_locations()` | Redis location store와 자동 peer 정책 등록 | [11](11-registry.ko.md) |
| `http()` | `listen` · `configure_tls` · `map_get/post/...<T>` · `use<TMiddleware>` · `map_health/readiness/liveness` | [6](06-http-hosting.ko.md) |
| `configure_worker()` | `min_threads` · `max_threads` · `idle_timeout` · `max_queue_length` 설정 | [3 §6.2](03-concepts.ko.md) |

**`module_t`** (`configuration/module.hpp`): 가상 `configure_services` · `configure_zlink` ·
`configure_handlers` · `configure_monitoring`. `app.add_zlink_framework<TModule>(...)`로 붙인다.

`zlink_builder_t`는 `add_route_mesh(...)`, `add_fanout_channel(...)`, `stream(...)`과
location store 등록을 소유한다. RouteMesh manual peer는
`mesh_node_builder_t::peer_connections()`에서 구성한다.

**DI**: `service_collection_t`/`service_provider_t`(`get_required<T>()`),
`service_lifetime_t{singleton, scoped, transient}`,
`service_scope_kind_t{handler_invocation, stream_session, spot_activation, entry_spot, actor_creation}`.

## 3. SPOT — spot · context · 생성/조회 · handler

| 타입 | 핵심 표면 |
|------|----------|
| `spot_t` / `entry_spot_t` | 상속 베이스. `configure(spot_context_t&)` 구현 |
| `spot_context_t` | `mesh_name()`/`node_rid()`/`spot_rid()`/`spot_name()`, `handlers()`, `close()`, `publish<TEvent>(channel,topic,event)`, `request_to_spot<TReply,TReq>(handle,req)`, `send_to_spot<TMsg>(handle,msg)`, `run_cpu_worker<TResult>(work)`, `run_io_worker<TResult>(work)`, `add_timer<THandler>(name,period,opts)` |
| `entry_spot_context_t` | `destroy_actor(actor_ref)` |
| `spot_handler_registry_t` | `add_handler<&M>([packet])` · `add_subscribe<&M>(topic)` |
| `mesh_node_builder_t` | `add_spot<TSpot>(name)` · `add_entry_spot<TEntrySpot>()` · `add_actor_factory<F>(type)` |
| `spot_manager_t` | `create_spot` · `get_or_create_spot` · `find_spot` · `list_spots` · `close_spot` |

**생성 결과**: `spot_create_result_t{spot_rid, spot_create_state_t, optional<zlink::framework::message_t> reply,
spot_context_t}`; `spot_create_state_t{existing, created, rejected}`.

**outbound 세 표면**(모두 `spot_context_t`): Spot direct `send_to_spot<TMsg>`,
`request_to_spot<TReply,TReq>`, ChannelName Logical Multicast `publish<TEvent>`.

## 4. Actor — actor · context · bound session · relay

| 타입 | 핵심 표면 |
|------|----------|
| `actor_ref_t` | `node_rid()`/`actor_type()`/`actor_id()`/`generation()`/`empty()` |
| `actor_context_t` | `mesh_name()`, `spot_rid()`, `handlers()`, `bound_session()`, `join_spot(spot_rid, DTO/message)`, `join_entry_spot(node_rid, DTO/message)`, `leave_spot()` |
| `bound_session_t` | `send(zlink::message_t)` · `send<TMsg>(msg)` · `disconnect()` — actor 가 자기 client 로 push |
| `session_actor_t` | `relay(zlink::message_t)` · `notify_disconnected()` |
| `session_actor_manager_t` | `create(type,id[, create_request])` · `find(id)` · `get_or_create(type,id[, create_request])` · `bind(actor_ref)` (DI 주입). 반환값은 application actor 객체가 아니라 `session_actor_t` handle |

- **factory**: `mesh_node_builder_t::add_actor_factory<TFactory>(actor_type)`로 등록한다.
- Node·ChannelName·Spot·Actor 메시지는 owner MeshNode의 같은 RouteMesh 계약을 사용한다.
- "ensure" 명명 계약은 없다 — `get_or_create*` 의미로 대신한다.

## 5. STREAM session — session · stream · header

| 타입 | 핵심 표면 |
|------|----------|
| `packet_stream_session_t` | `on_connected/on_disconnected/on_error/on_packet` callback(모두 `task_t<void>`) |
| `stream_t` | `session_id()` · `close()` · `write_packet(payload)` · `reply_packet(payload)` |
| `stream_dispatch_context_t` | `packet_name()` · `metadata()` · `can_reply()` |
| `stream_error_t` | `error()` · `native_code()` · `message()` |
| `stream_node_options_builder_t` | `bind(endpoint)` · `set_tls_server(...)` · `enable_actor_dispatch(mesh_name)` · `register_session<TSession>()` |

옵션 레벨의 `register_session<TSession>()`은 `packet_stream_session_t` 파생 타입을 연결당
하나 생성하도록 등록한다. `enable_actor_dispatch(mesh_name)`은 Actor dispatch를 사용하는
session이 참조할 local MeshNode를 선택한다. Actor에서 client로 보내는 push는 Actor 도메인의
`bound_session_t`(§4)를 사용한다. 코덱 값은
`stream_codec_t{raw,json,message_pack,protobuf}`다.

## 6. Location store — descriptor · owner · revision

| 타입 | 핵심 표면 |
|------|----------|
| `location_store_t` | MeshNode·Spot·Actor location과 owner lease store를 결합한 interface |
| `redis_location_store_t` | `redis_location_options_t`로 구성하는 production store |
| `mesh_node_descriptor_t` | MeshName·RID·endpoint·ChannelName membership·revision snapshot |
| `location_change_stamp_store_t` | scope별 변경 stamp 조회 |
| `routing_id_slot_allocation_store_t` | allocated RID slot acquire·renew·release·snapshot |

`zlink_builder_t::add_location_store(...)`로 store를 등록하고
`configure_locations()`에서 자동 peer 정책을 설정한다. Manual peer는
`mesh_node_builder_t::peer_connections()`가 소유한다.

## 7. Monitoring — message flow · runtime error · health

| 빌더 | 핵심 표면 |
|------|----------|
| `dispatch_options_t` | `message_flow(mode)` · `set_message_flow_observer(...)` · `set_runtime_error_sink(...)` |
| `app_t` | `set_message_flow_mode(mode)` · MeshNode snapshot/observe/drain runtime control |
| `metrics_builder_t` | `add_runtime_metrics()` |
| `health_builder_t` | `add_zlink_runtime_check` · `add_channel_check` · `add_location_check` · `add_stream_endpoint_check` · `add_hosted_service_check` · `report()` |

`message_flow_event_t`는 dispatch 결과 snapshot이고 `runtime_error_event_t`는 observer나
runtime 경계 실패를 별도 sink에 전달한다.

## 8. Timer — `timers/timer.hpp`

- `spot_context_t::add_timer<THandler>(name, std::chrono::milliseconds period, timer_options_t={})` → `timer_t`.
- `timer_t`: `cancel()` · `is_disposed()`.
- `timer_options_t{overrun_policy, max_catch_up_ticks=1, stop_on_unhandled_exception=false}`.
- `timer_overrun_policy_t{skip_late_ticks(기본), catch_up_bounded, delay_next_tick}`.
- `timer_tick_t{name, delivery_index, scheduled_index, period, scheduled_elapsed, started_elapsed, delay, skipped_ticks}`.

## 9. 공통 타입

| 타입 | 의미 |
|------|------|
| `task_t<T>` / `task_t<void>` | 비동기 값 — 런타임에서는 `co_await`, 테스트에서는 `.result()` |
| `result_t<T>` | 성공/실패 래퍼 — `operator bool` · `value()` · `error()` · `error_kind()` |
| `framework_exception_t` | `kind()` · `code()` · `what()` · `is_retriable()` |
| `framework_error_kind_t` | Actor·Spot·route·handler·payload·request·worker 실패의 framework 분류 |
| `framework_exception_t::code()` | timeout, shutdown, disconnected, closed, cancelled 같은 경계 상태의 `std::error_code` |
| `zlink::framework::message_t` | framework 메시지. DTO 또는 encoded payload를 들고 codec registry로 encode/decode한다 |
| `zlink::message_t` | raw 메시지 (typed 경계 밖에서만; `bindings/cpp`에 정의) |
| `zlink::routing_id_t` | 노드/스팟 논리 주소 (`bindings/cpp`에 정의) |
| `serializer_registry_t` / `serializer_t<T>` | codec 등록·직렬화 |
| `worker_call_t<TResult>` | `spot_context_t::run_cpu_worker` 또는 `run_io_worker` 반환 |

> **없는 것(다른 언어와 대비)**: cpp framework 에는 `clustering` API, `Stage wrapper` 타입이
> 없다(응용이 SPOT 위에 직접 구성). dotnet 의 attribute 기반 자동 등록도 없다 — cpp 는
> 어노테이션·리플렉션이 없어 **수동 등록만**이다(언어 특성).

## 10. 더 보기

- 채널/스팟/액터/스트림 사용법: [7](07-channel-messaging.ko.md)·[8](08-spot.ko.md)·[9](09-actor-session.ko.md)·[10](10-stream.ko.md)
- 실행 가능한 전체 예제: [14장 샘플 맵](14-samples-map.ko.md)

[다음: 샘플 지도 →](14-samples-map.ko.md)
