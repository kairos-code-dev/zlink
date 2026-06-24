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
| `channel_client_t` | `request(channel, req)` / `request_to_channel(channel, req)` → `channel_request_call_t` | DI 주입 |
| `request_client_t` | `request(req)` → `channel_request_call_t` (channel 고정) | `zlink_builder_t::request_client(name)` |
| `publisher_t` | `publish(channel, topic, event)` → `send_call_t` | `zlink_builder_t::publisher()` |
| `message_bus_t` | `request(channel, req)` → `channel_request_call_t` · `send(channel, msg)` → `send_call_t` · `publish(channel, topic, event)` → `send_call_t`; `pending_count()`/`pending_limit()` | `zlink_builder_t::message_bus()` |
| `route_client_t` | `send(router_channel, routing_id_t target, msg)` → `route_send_call_t` · `request(router_channel, routing_id_t target, req)` → `route_request_call_t` | `zlink_builder_t::route_client(serializers)` |

**종결 호출(call) 객체** (`channels/call.hpp`, route 계열은 `channels/channel.hpp`) —
전송 전 옵션을 얹는다:
`channel_request_call_t`는 `.timeout()` · `.packet_name()` · `.metadata(k,v)` · `.async<TReply>()`,
`send_call_t`는 `.timeout()` · `.packet_name()` · `.metadata(k,v)` · `.async()`,
`route_request_call_t`는 `.timeout()` · `.packet_name()` · `.metadata(k,v)` · `.async()` /
`.async<TReply>()`, `route_send_call_t`는 `.packet_name()` · `.metadata(k,v)` · `.async()`를
제공한다. send 계열은 `task_t<void>`를 반환한다.

**handler/filter 계약** (`handlers/handler_registry.hpp`):

| 항목 | 표면 |
|------|------|
| handler 종류 | `handler_kind_t{request, send, event, raw}` |
| handler context | `request_context_t` · `send_context_t` · `publish_context_t`(topic/source) |
| filter 계약 | `TFilter::invoke(const handler_invocation_context_t&, handler_next_t)` — `next()` 호출 안 하면 handler 미실행 |
| `handler_invocation_context_t` | `{descriptor, context, shared_ptr<const message_t> message}` |
| 등록 | `options.handlers().add<T>("group")` / `use_filter<TFilter>()` |

**codec 등록** (`codecs/serializer.hpp`): `serializer_registry_t` — JSON 기본 serializer는
`add_json<T>()`로 등록한다. Protobuf와 MessagePack은 framework codec extension package를
참조한 뒤 `codecs().use(extension)`으로 등록한다. custom codec도 같은 extension 객체 안에서
`add_serializer<T>(serialize_fn, deserialize_fn)`를 호출해 등록한다.

route mesh 핸들러는 `route_channel_builder_t::add_send_handler`/`add_request_handler`
(첫 인자가 payload, 둘째가 `route_handler_context_t`)로 등록한다.

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
| `handlers()` | 핸들러 그룹 등록 (`add<T>("group")`) | [3 §6.1](03-concepts.ko.md) |
| `codecs()` | `add_json` / `use(extension)` / `add_serializer<T>` | [7 §2](07-channel-messaging.ko.md) |
| `add_client_server_channel(name)` | `enable_server(ep)` · `server_routing_id(rid)` · `enable_client([ep])` · `use_handler_group(g)` | [7](07-channel-messaging.ko.md) |
| `add_fanout_channel(name)` | `enable_publisher(ep)` · `enable_subscriber([ep])` · `use_handler_group(g)` | [7 §6](07-channel-messaging.ko.md) |
| `add_route_mesh(name)` | `enable_server(ep)` · `set_routing_id(rid)` · `enable_client([ep])` (ROUTER 공유, SpotMesh와 같은 프로세스면 자동 bridge) | [7 §7](07-channel-messaging.ko.md) |
| `add_spot_mesh(name)` | `bind` · `enable_router` · `enable_pub_sub` · `use_discovery` · `add_entry_spot<T>` · `add_spot<T>` · `add_actor_factory<F>` · `enable_actor_gateway` | [8](08-spot.ko.md)·[9](09-actor-session.ko.md) |
| `http()` | `listen` · `configure_tls` · `map_get/post/...<T>` · `use<TMiddleware>` · `map_health/readiness/liveness` | [6](06-http-hosting.ko.md) |
| `use_discovery()` | `add_registry_endpoint(ep)` | [11](11-registry.ko.md) |
| `enable_registry(pub, router)` | registry 서버 | [11 §2](11-registry.ko.md) |
| `handler_coroutine_workers(n)` | 코루틴 핸들러 worker 수 | [3 §6.2](03-concepts.ko.md) |

**`module_t`** (`configuration/module.hpp`): 가상 `configure_services` · `configure_zlink` ·
`configure_handlers` · `configure_monitoring`. `app.add_zlink_framework<TModule>(...)`로 붙인다.

**저수준 빌더** `zlink_builder_t` (`configuration/zlink_builder.hpp`, `app.advanced().zlink()`):
`add_node(name)` · `channel(name)` · `route_channel(name)` · `add_spot_node(name)` ·
`stream(name)` · `discovery()` · `enable_registry()`; 접근자 `message_bus()` ·
`publisher()` · `request_client(name)` ·
`route_client(serializers)` · `registry_query()`. 채널 capability 는 `capability_builder_t`
(`bind`/`connect`/`use_discovery`)로 구성한다.

**DI**: `service_collection_t`/`service_provider_t`(`get_required<T>()`),
`service_lifetime_t{singleton, scoped, transient}`,
`service_scope_kind_t{handler_invocation, stream_session, spot_activation, entry_spot, actor_creation}`.

## 3. SPOT — spot · context · 생성/조회 · handler

| 타입 | 핵심 표면 |
|------|----------|
| `spot_t` / `entry_spot_t` | 상속 베이스. `configure(spot_context_t&)` 구현 |
| `spot_context_t` | `node_rid()`/`spot_rid()`/`spot_name()`, `handlers()`, `close()`, `publish<TEvent>(topic,event)`, `request_to<TReply,TReq>(node_rid,spot_rid,req)`, `send_to<TMsg>(...)`, `run_worker<TWork>(work)`, `add_timer<THandler>(name,period,opts)` |
| `entry_spot_context_t` | + `destroyActor<TActor>(ref, actor)` |
| `spot_handler_registry_t` | `add_handler<&M>([packet])` · `add_subscribe<&M>(topic)` · `add_actor_packet<&M>([packet])` |
| `spot_node_builder_t` | `add_spot<TSpot>(name)` · `add_entry_spot<TEntrySpot>()` · `add_actor_factory<F>(type)` · **`create_spot(name[,DTO/message])`** · **`get_or_create_spot(name, spot_rid[,DTO/message])`** · `find_spot`/`list_spots`/`close_spot` |

**생성 결과**: `spot_create_result_t{spot_rid, spot_create_state_t, optional<zlink::framework::message_t> reply,
spot_context_t}`; `spot_create_state_t{existing, created, rejected}`.

**outbound 3표면**(모두 `spot_context_t`): local/routed `send_to<TMsg>`, routed
`request_to<TReply,TReq>`, fanout `publish<TEvent>`.

## 4. Actor — actor · context · bound session · gateway

| 타입 | 핵심 표면 |
|------|----------|
| `actor_ref_t` | `node_rid()`/`actor_type()`/`actor_id()`/`generation()`/`empty()` |
| `actor_context_t` | `actor_ref()`, `is_joined()`, `bound_session()`, `join_spot(spot_rid, DTO/message)`, `join_entry_spot(node_rid, DTO/message)`, `join_spot_raw(...)`, `join_entry_spot_raw(...)` |
| `bound_session_t` | `send(zlink::message_t)` · `send<TMsg>(msg)` · `disconnect()` — actor 가 자기 client 로 push |
| `session_actor_t` | `ref()`/`actor_id()`/`context()`/`bound_session()`, `relay(zlink::message_t)`/`relay_request(zlink::message_t)`, `notify_disconnected()` |
| `session_actor_manager_t` | `create(type,id[, create_request])` · `find(id)` · `get_or_create(type,id[, create_request])` · `bind(actor_ref)` · `unbind_session(id)` (DI 주입). 반환값은 application actor 객체가 아니라 `session_actor_t` handle |

- **factory**: `spot_node_builder_t::add_actor_factory<TFactory>(actor_type)`로 등록(duck-typed: `create(actor_id)`).
- **gateway**: 공개 `actor_gateway_t` 클래스는 없다. `spot_node` 쪽 `enable_actor_gateway()` +
- "ensure" 명명 계약은 없다 — `get_or_create*` 의미로 대신한다.

## 5. STREAM session — session · stream · header

| 타입 | 핵심 표면 |
|------|----------|
| `packet_stream_session_t` | 순수 가상: `on_connected/on_disconnected/on_error/on_packet` (모두 `task_t<void>`) |
| `stream_t` | `session_id()` · `close()` · `write_packet(payload)` · `reply_packet(payload)` |
| `stream_dispatch_context_t` | `packet_name()` · `metadata()` · `can_reply()` |
| `stream_error_t` | `error()` · `native_code()` · `message()` |

옵션 레벨 등록: `stream_node_options_builder_t::register_session<TSession>()`(`packet_stream_session_t`
actor → client push 는 Actor 도메인의 `bound_session_t`(§4). 코덱 `stream_codec_t{raw,json,
message_pack,protobuf}`.

## 6. Registry — server · query · topology

| 타입 | 핵심 표면 |
|------|----------|
| `registry_builder_t` (서버) | `registry_id` · `bind(pub_ep, router_ep)` · `heartbeat_interval/timeout` · `broadcast_interval` · `add_peer(pub_ep)` |
| `discovery_builder_t` | `connect_registry(ep)` |
| `registry_query_t` (in-process) | `status()` · `service_summary([filter])` · `topology([filter])` · `member_peers(channel)` · `resolve_spot_remote_address(spot_rid)` |
| `registry_query_client_t` (원격) | `connect(options\|endpoint)` · `topology([filter])` · `close()` |

- 타입: `member_peer_t{channel_name, node_name, endpoint}`, `topology_entry_t`/`topology_filter_t`,
  `topology_source_t{embedded, remote}`, `service_role_t{server, client, publisher, subscriber, spot_node, stream_endpoint}`.
- **배포 모델**: embedded(`enable_registry` — Registry+서비스 한 프로세스) vs standalone
  (`registry_query_client_t`로 별도 프로세스 조회). `topology_source_t`가 둘을 구분한다.
- **clustering 전용 API 는 없다** — 다중 registry 는 `add_peer`(peer pub endpoint)로만 묶는다.

## 7. Monitoring — event · health · metric

| 빌더 | 핵심 표면 |
|------|----------|
| `monitoring_builder_t` | `add_socket_events(src[,kinds])` · `add_registry_events(src,interval)` · `add_spot_events(src,interval)` · `add_spot_timer_events(src)` · `add_stream_events`/`add_actor_events`/`add_discovery_events` · `on<TEvent>(handler)` · `on_trace(hook)` |
| `metrics_builder_t` | `add_runtime_metrics()` · `record_runtime_metric(name, value, tags={})` |
| `health_builder_t` | `add_zlink_runtime_check` · `add_channel_check` · `add_registry_check` · `add_stream_endpoint_check` · `add_hosted_service_check` · `set_status` · `report()` |

이벤트 payload: `socket/discovery/registry/spot/stream/actor_event_payload_t`,
`metric_event_payload_t`. 상태값 `health_status_t{healthy, degraded, unhealthy}`,
`runtime_event_severity_t{trace, info, warning, error}`.

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
| `framework_exception_t` | `kind()` · `what()` · `is_retriable()` |
| `framework_error_kind_t` | `request_protocol_error` · `request_failed` · `timeout` · `payload_decode_failed` · `handler_not_found` · `actor_*` · `spot_*` · `closed`/`disconnected`/`shutdown` 등 |
| `zlink::framework::message_t` | framework 메시지. DTO 또는 encoded payload를 들고 codec registry로 encode/decode한다 |
| `zlink::message_t` | raw 메시지 (typed 경계 밖에서만; `bindings/cpp`에 정의) |
| `zlink::routing_id_t` | 노드/스팟 논리 주소 (`bindings/cpp`에 정의) |
| `serializer_registry_t` / `serializer_t<T>` | codec 등록·직렬화 |
| `worker_call_t<TResult>` | `spot_context_t::run_worker` 반환 |

> **없는 것(다른 언어와 대비)**: cpp framework 에는 `clustering` API, `Stage wrapper` 타입이
> 없다(응용이 SPOT 위에 직접 구성). dotnet 의 attribute 기반 자동 등록도 없다 — cpp 는
> 어노테이션·리플렉션이 없어 **수동 등록만**이다(언어 특성).

## 10. 더 보기

- 채널/스팟/액터/스트림 사용법: [7](07-channel-messaging.ko.md)·[8](08-spot.ko.md)·[9](09-actor-session.ko.md)·[10](10-stream.ko.md)
- 실행 가능한 전체 예제: [14장 샘플 맵](14-samples-map.ko.md)

[다음: 샘플 지도 →](14-samples-map.ko.md)
