[← 목차](./README.ko.md)

# 12. 인터페이스 카탈로그

자주 쓰는 표면을 한 곳에 모은 레퍼런스다. 사용법은 각 장을 본다.

## 1. 확장 지점 — 사용자가 구현하는 것

| 확장 지점 | 형태 | 장 |
|-----------|------|-----|
| 채널/HTTP 핸들러 | `request_type`/`reply_type`/`topic_name` + `handle()` (동기 또는 `task_t`) | [3](./03-concepts.ko.md)·[5](./05-channel-messaging.ko.md)·[9](./09-http-hosting.ko.md) |
| 핸들러 DI | `using dependency_types = dependency_list_t<...>` + 생성자 주입 | [3 §3](./03-concepts.ko.md) |
| spot | `spot_t`/`entry_spot_t` 상속 + `configure(spot_context_t&)` | [6](./06-spot.ko.md) |
| spot actor 패킷 핸들러 | `(const actor_t&, spot_actor_request_context_t&, const req_t&) → res_t` | [6 §3](./06-spot.ko.md) |
| actor + factory | actor struct + `create(actor_id)` factory | [7 §2](./07-actor-session.ko.md) |
| stream session | `packet_stream_session_t` 상속 — `on_connected/packet/error/disconnected` | [8 §3](./08-stream.ko.md) |
| HTTP middleware | `use<TMiddleware>()` — before/after 훅 | [9 §4](./09-http-hosting.ko.md) |
| hosted service | `hosted_service_t` — `start(provider)`/`stop()` | [3 §1](./03-concepts.ko.md) |
| module | `module_t` — `configure_services/zlink/handlers/monitoring` | [3 §6](./03-concepts.ko.md) |
| 설정 바인딩 | `static T bind(const configuration_section_t&)` | [4 §5](./04-configuration.ko.md) |
| 메시지(packet) | struct + `static constexpr const char *packet_name` (+codec ADL) | [2 §2](./02-getting-started.ko.md) |

## 2. app_t 표면

| 메서드 | 역할 |
|--------|------|
| `create()` | 앱 생성 |
| `config()` / `logging()` / `monitoring()` / `metrics()` / `health()` | 부속 구성 |
| `add_zlink_framework(람다)` / `add_zlink_framework<TModule>(...)` | zlink 토폴로지 선언 |
| `add_module(module)` / `add_hosted_service(service)` | 패키징/백그라운드 |
| `run(argc, argv)` | 실행 (블로킹, 종료 코드 반환) |
| `stop()` / `request_stop()` | 종료 (동기/비동기 요청) |
| `advanced()` | services/handlers/zlink builder 직접 접근 |

## 3. zlink_framework_options_t 표면

| 진입점 | 내용 | 장 |
|--------|------|-----|
| `services()` | DI 등록 (`add_singleton<T>` 등) | [3 §3](./03-concepts.ko.md) |
| `handlers()` | 핸들러 그룹 등록 (`add<T>("group")`) | [3 §4](./03-concepts.ko.md) |
| `codecs()` | `add_json()` / `add_message_pack()`(+typed) / `add_protobuf()` | [5 §2](./05-channel-messaging.ko.md) |
| `add_client_server_channel(name)` | `enable_server(ep)` · `enable_client([ep])` · `use_handler_group(g)` | [5](./05-channel-messaging.ko.md) |
| `add_fanout_channel(name)` | `enable_publisher(ep)` · `use_handler_group(g)` | [5 §4](./05-channel-messaging.ko.md) |
| `add_dealer_mesh_channel(name)` | 동격 노드 분산 | [5](./05-channel-messaging.ko.md) |
| `add_route_mesh_channel(name)` | `bind` · `set_routing_id` · `connect` · `enable_spot_route_egress` | [5 §5](./05-channel-messaging.ko.md) |
| `add_spot_mesh(discovery).add_node(name)` | `enable_router` · `enable_pub_sub` · `use_discovery` · `accept_routes_from_channel` · `attach_channel_client/publisher` · `add_entry_spot<T>` · `add_spot<T>` · `add_actor_factory<F>` · `enable_actor_gateway` | [6](./06-spot.ko.md)·[7](./07-actor-session.ko.md) |
| `add_stream_node(name)` | `bind` · `register_session<T>` · `attach_actor_gateway` | [8](./08-stream.ko.md) |
| `http()` | `listen` · `configure_tls` · `configure_server` · `map_get/post/put/delete<T>` · `use<TMiddleware>` · `map_health/readiness/liveness` | [9](./09-http-hosting.ko.md) |
| `use_discovery()` | `add_registry_endpoint(ep)` | [10](./10-registry.ko.md) |
| `enable_registry(pub, router)` | registry 서버 | [10 §2](./10-registry.ko.md) |
| `use_registry_spot_remote_addresses([channel])` | spot 원격 주소를 registry로 | [10 §4](./10-registry.ko.md) |
| `handler_coroutine_workers(n)` | 코루틴 핸들러 worker 수 | [3 §5](./03-concepts.ko.md) |

## 4. 주입 가능한 프레임워크 서비스

`dependency_types`로 받을 수 있는 대표 타입들.

| 타입 | 용도 | 장 |
|------|------|-----|
| `channel_client_t` | 채널 request (request-reply) | [5 §3](./05-channel-messaging.ko.md) |
| `session_actor_manager_t` | 세션-액터 바인딩 관리 | [7 §4](./07-actor-session.ko.md) |
| `logger_t<TOwner>` | 소스 이름 붙은 로거 | [11 §1](./11-monitoring.ko.md) |
| 사용자 싱글톤 | `options.services().add_singleton<T>(...)`로 올린 타입 | [3 §3](./03-concepts.ko.md) |

publish/send는 DI 주입이 아니라 `zlink_builder_t`에서 얻는 `publisher_t` /
`message_bus_t`로 한다 — [5 §3](./05-channel-messaging.ko.md).

## 5. 공통 타입

| 타입 | 의미 |
|------|------|
| `task_t<T>` | 비동기 값 — `co_await` 또는 `.result()` |
| `result_t<T>` | 성공/실패 래퍼 — `operator bool` / `value()` / `error()` |
| `framework_exception_t` | `kind()` / `what()` / `is_retriable()` |
| `framework_error_kind_t` | `request_protocol_error` · `request_failed` · `timeout` · `payload_decode_failed` · `closed` 등 |
| `zlink::message_t` | raw 메시지 (typed 경계 밖에서만) |
| `timer_t` / `timer_options_t` / `timer_tick_t` | spot timer ([6 §5](./06-spot.ko.md)) |

[다음: 샘플 지도 →](./13-samples-map.ko.md)
