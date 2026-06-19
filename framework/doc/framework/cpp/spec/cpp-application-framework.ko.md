<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Draft -- ZLink Framework C++ Policy](../internals/cpp-framework-policy.ko.md) | [다음: Spec -- ZLink Framework C++ Interface Design](cpp-framework-interfaces.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../common/README.ko.md)

[C++ 묶음](../README.ko.md) | [C++ 정책](../internals/cpp-framework-policy.ko.md) | [Framework 인터페이스](cpp-framework-interfaces.ko.md) | [HTTP Client](../../../http-client/cpp/README.ko.md) | [HTTP Hosting](cpp-http-hosting.ko.md)

# Spec -- ZLink Framework C++ Application Framework

> 이 문서는 **구현 완료된 설계 계약**이다.
> `C++`용 `ZLink Framework`를 어떤 application framework로
> 만들지 정리한다.

## 1. 제품 포지션

`ZLink Framework for C++`는 단순 zlink binding helper나 sample framework가 아니다.
목표 포지션은 `.NET Core`/`ASP.NET Core`, `Spring Boot`, `NestJS`와 같은 application
framework다. 주 벤치마크는 `.NET Core`다. 구체적으로 host, DI, configuration,
logging, lifecycle은 `.NET Generic Host` 계열을 기준으로 삼고, HTTP routing과 handler
표면은 `ASP.NET Core Minimal API`를 기준으로 삼는다. `Spring Boot`와 `NestJS`는 기능
축을 확인하는 보조 기준으로만 사용한다.

따라서 C++ framework는 아래 기능을 core framework가 제공해야 한다.

- application host
- DI container
- configuration
- HTTP hosting
- zlink messaging
- handler model
- middleware와 filter
- logging
- observability
- validation
- error handling
- security/auth extension point
- scheduling과 background work
- developer convenience

각 기능은 따로 노는 API가 아니라 같은 app framework 안에서 같은 DI, configuration,
logging, error, validation, lifecycle 모델을 공유해야 한다. HTTP handler, zlink message
handler, SPOT handler, STREAM session handler, timer handler, hosted service가 서로 다른
프레임워크처럼 보이면 실패다.

최종 완료 기준은 아래 세 가지다.

- `.NET` framework와 동일한 구조를 가진다.
- `.NET` framework와 동일한 기능을 제공한다.
- `.NET` framework와 동일 수준의 사용성을 제공한다.

여기서 동일하다는 말은 C++ 문법과 RAII, coroutine, CMake/package 같은 언어별 표현까지
같아야 한다는 뜻이 아니다. 사용자가 보는 application 구성 구조, 기능 범위, handler 작성
경험, 테스트 가능한 동작 기대값이 `.NET` framework와 같은 수준이어야 한다는 뜻이다.

## 2. 벤치마크 기준

주 벤치마크는 `.NET Core`/`ASP.NET Core`다. HTTP 기능만 보는 것이 아니라, host, DI,
configuration, logging, lifecycle, hosted service, validation, error handling까지 같은
application model로 묶이는지를 함께 본다.

| .NET Core / ASP.NET Core 개념 | C++ framework 대응 |
|-------------------------------|--------------------|
| `WebApplicationBuilder` | `app_t::create()`와 builder/options |
| `IHost` / `IHostedService` | `app_t`, `hosted_service_t` |
| `IServiceCollection` | `service_collection_t` |
| `IServiceProvider` | `service_provider_t` |
| service lifetime | `singleton`, `scoped`, `transient` |
| `IConfiguration` | `config_builder_t`, typed options binding |
| Options pattern | `config_builder_t::bind<T>()` / `bind_required<T>()` |
| Minimal API route handler | `options.http().map_get/map_post/map_put/map_delete<THandler>(...)` |
| model binding | DTO JSON, route parameter, query string binding |
| middleware | HTTP middleware, zlink handler filter |
| filters | validation/auth/exception/logging filter |
| `ILogger<T>` | category logger |
| health checks | health/readiness/liveness endpoint와 runtime health |
| metrics/tracing | monitoring event, metrics, trace/correlation |
| `CancellationToken` | host shutdown, timeout, drain policy |
| standard error response | framework error JSON |

Spring Boot와 NestJS에서 확인할 축은 아래 정도로 제한한다.

- module/controller/provider 구성 단위
- global filter, interceptor, pipe에 해당하는 cross-cutting extension
- configuration profile과 validation
- testing module과 local dev runner

## 3. Application Host

host는 모든 runtime 기능의 lifecycle owner다.

필수 기능:

- `app_t::create()`
- `app.run(argc, argv)`
- `app.stop()` / `request_stop()`
- signal handling
- graceful shutdown
- hosted service start/stop
- reverse stop order
- startup validation
- readiness/liveness state
- environment/profile selection
- run failure exit code

완료 기준:

- app가 시작되기 전 DI, HTTP route, zlink topology, serializer, handler 등록을 검증한다.
- startup validation 실패는 runtime thread를 만들기 전에 예외로 닫는다.
- shutdown 중 새 HTTP request, zlink submit, timer tick이 무기한 생성되지 않는다.
- hosted service와 HTTP server는 app lifecycle에 묶이고, 사용자가 별도 thread join을 직접
  하지 않는다.

## 4. DI Container

DI는 framework core 기능이다. 외부 DI 라이브러리를 public dependency로 두지 않는다.

필수 기능:

- singleton / scoped / transient
- constructor injection
- request scope
- handler invocation scope
- STREAM session scope
- SPOT activation scope
- timer handler scope
- hosted service scope
- required service(`get_required<T>()`; 미등록 service는 예외)
- duplicate registration validation
- shutdown 중 resolve 금지

사용 예:

```cpp
options.services()
  .add_singleton<sample_topology_t>()
  .add_transient<create_game_http_handler_t>();
```

handler auto registration은 handler 타입의 `dependency_types`를 사용한다.

```cpp
class create_game_http_handler_t {
public:
    using request_type = create_game_http_req_t;
    using reply_type = create_game_http_res_t;
    using dependency_types =
      dependency_list_t<channel_client_t, sample_topology_t,
                        logger_t<create_game_http_handler_t>>;
};
```

## 5. Configuration

configuration은 `.NET Core`의 Generic Host configuration model을 벤치마크로 삼는다.

필수 기능:

- JSON file
- environment variables
- CLI args
- environment/profile: `development`, `production`, `test`
- strongly typed options binding
- config validation
- missing required value detection
- endpoint config helper
- sample topology config

권장 표면:

```cpp
app.config()
  .load_json("appsettings.json")
  .load_json("appsettings.development.json", optional_t::yes)
  .load_env("ZLINK_")
  .load_cli(argc, argv);

auto server = app.config().bind_required<server_options_t>("server");
```

결정 사항:

- secret manager와 reload-on-change는 초기 core 필수 기능이 아니다.
- 다만 public config model이 secret provider와 reload provider를 막지 않아야 한다.
- reload는 HTTP/zlink runtime 재구성까지 의미가 복잡하므로 별도 goal로 분리한다.

## 6. HTTP Hosting

HTTP hosting은 framework core 기능이다. ASP.NET Core Minimal API의 `MapGet`, `MapPost`,
`MapPut`, `MapDelete`를 주 기준으로 삼는다.

필수 기능:

- `listen("http://...")`
- `listen("https://...")`
- HTTPS TLS certificate/private key option
- `map_get`
- `map_post`
- `map_put`
- `map_delete`
- JSON body binding
- route parameter binding
- query string binding
- response DTO serialization
- status code response
- standard error response
- request scope
- HTTP middleware/filter
- HTTP request logging
- graceful shutdown

기본 예:

```cpp
options.http()
  .listen(topology.api_http_endpoint)
  .map_post<create_game_http_handler_t>("/games");
```

handler는 DI로 생성하고 DTO를 반환한다.

```cpp
class create_game_http_handler_t {
public:
    using request_type = create_game_http_req_t;
    using reply_type = create_game_http_res_t;
    using dependency_types =
      dependency_list_t<channel_client_t, sample_topology_t,
                        logger_t<create_game_http_handler_t>>;

    task_t<create_game_http_res_t> handle(const create_game_http_req_t &request);
};
```

HTTP hosting은 HTTP와 HTTPS를 모두 지원한다. HTTPS는 HTTP hosting의 TLS option으로
설정하며, public API에는 certificate/private key 같은 설정 값만 노출한다. SSL context,
SSL stream, OpenSSL 구현 타입은 runtime 내부에 숨긴다. WebSocket은 HTTP hosting이 아니라
STREAM Connector 또는 transport 영역에서 다룬다.

## 7. ZLink 기능

zlink 기능은 framework의 핵심 runtime 기능이다.

필수 기능:

- channel server/client
- request/reply
- send/event
- pub/sub
- route channel
- SPOT
- STREAM
- ActorGateway
- timer
- registry/discovery
- monitoring event
- graceful drain

connector는 framework와 별도 배포되는 client-side library다. framework server core는
connector API에 의존하지 않는다.

## 8. Handler Model

모든 handler는 같은 규칙을 사용한다.

handler 종류:

- HTTP handler
- channel message handler
- SPOT handler
- STREAM session handler
- timer handler
- hosted service

공통 규칙:

- DTO type alias
- `dependency_types`
- 생성자 주입
- sync 반환과 `task_t<T>` 반환
- framework error mapping
- logging category
- validation hook
- filter pipeline
- request/handler scope

이 규칙을 지키면 HTTP에서 zlink request를 보내는 handler와 zlink message를 처리하는
handler가 같은 읽기 수준을 갖는다.

## 9. Middleware 와 Filter

framework는 cross-cutting concern을 handler 안에 반복하지 않도록 middleware/filter를 제공한다.

필수 기능:

- HTTP middleware
- zlink message filter
- handler filter
- logging filter
- validation filter
- auth filter
- exception filter

HTTP core 구현은 typed JSON handler, raw HTTP request handler, `http_response_t` 반환
handler, `options.http().use<TMiddleware>()`, `http_context_t` 기반 before/after hook을 모두
포함한다. route별 HTTP filter 타입은 별도 public API로 만들지 않고, middleware가 method/path를
보고 필요한 route에만 적용한다. middleware는 `http_context_t::json_response(...)`로 handler
호출을 건너뛰는 short-circuit response를 만들 수 있다.

초기 구현 순서:

1. exception filter
2. logging filter
3. validation filter
4. auth filter
5. custom filter registration

filter와 middleware는 DI를 사용할 수 있어야 한다. filter가 native socket, Beast request,
CAPI handle을 직접 받으면 안 된다.

## 10. Logging

logging은 framework core 기능이다.

필수 기능:

- console logger
- file logger
- category logger
- structured log
- request id
- correlation id
- HTTP request log
- zlink message log
- handler latency log
- startup/shutdown log

HTTP correlation id와 framework handler log correlation id는 연결되어야 한다. 예를 들어
HTTP `POST /games`에서 `CreateGameHttpReq`를 처리할 때 같은 correlation id가 log와 monitoring
event에 남아야 한다.

## 11. Observability

운영자가 runtime 상태를 볼 수 있어야 한다.

필수 기능:

- health check
- readiness
- liveness
- metrics
- tracing
- runtime event
- handler latency
- queue depth
- request timeout count
- active HTTP request count
- active zlink pending request count
- connection state

HTTP health endpoint는 선택적으로 제공한다.

```cpp
options.http()
  .map_health("/health")
  .map_readiness("/ready")
  .map_liveness("/live");
```

health는 HTTP만의 상태가 아니라 zlink channel, discovery, registry, STREAM endpoint,
hosted service 상태를 함께 반영한다.

## 12. Validation

validation은 startup validation과 request validation으로 나눈다.

startup validation:

- duplicate route
- duplicate handler
- duplicate service
- missing serializer
- invalid endpoint
- channel group without server
- HTTP route handler shape mismatch
- scoped service를 root provider에서 resolve하는 설정 오류

request validation:

- invalid JSON
- missing required field
- route parameter parse failure
- query parse failure
- DTO validation failure
- unsupported content type

DTO validation은 초기에는 사용자 정의 `validate()` hook으로 시작하고, 추후 annotation 또는
schema 기반으로 확장한다.

## 13. Error Handling

framework error는 HTTP와 zlink 양쪽에서 같은 의미를 가져야 한다.

필수 기능:

- `framework_exception_t`
- `framework_error_kind_t`
- HTTP status mapping
- zlink error mapping
- standard JSON error response
- unhandled exception isolation
- retriable 여부
- internal detail masking

기본 HTTP error response:

```json
{
  "error": "payload_decode_failed",
  "message": "payload deserialization failed",
  "correlationId": "..."
}
```

## 14. Security / Auth

초기 완성 구현 범위가 아니더라도 설계 공간은 core에 있어야 한다.

필수 extension point:

- HTTP auth filter
- token extraction
- user principal/context
- zlink actor/session 인증과 연결
- route별 auth requirement
- handler에서 current user 조회

초기 구현은 auth filter extension point까지만 두고, JWT/OAuth 같은 구체 provider는 별도
extension으로 둔다.

## 15. Scheduling / Background Work

필수 기능:

- hosted service
- periodic timer
- delayed task
- graceful stop 가능한 worker
- thread pool / coroutine executor 설정
- CPU-bound offload guidance
- timer overrun policy

timer는 zlink CAPI timer와 framework timer를 모두 고려하되, public user model은
`timer_handler_t`와 `timer_tick_t`로 통일한다.

## 16. Developer Convenience

C++ framework는 설정이 불편하면 `.NET Core`/`ASP.NET Core`, Spring Boot, NestJS 같은
포지션이 될 수 없다.
따라서 개발 편의 기능도 core delivery 범위에 포함한다.

필수 기능:

- project template
- sample topology config
- CMake presets
- vcpkg manifest
- CLion 실행 설정
- Visual Studio CMake preset
- WSL 사용 guide
- test harness
- local dev runner
- generated config skeleton
- sample e2e script
- package install consumer test

`appsettings.json`, `appsettings.development.json`, environment variable, CLI override가 함께
동작하는 예제를 공식 sample에 포함한다.

## 17. 회귀 테스트 매트릭스

회귀 테스트는 기능별 단위 테스트와 application flow 테스트를 모두 포함한다.

| 축 | 필수 테스트 |
|----|-------------|
| contract header | `zlink/framework.hpp`, 세부 contract header compile |
| app host | run/stop, signal stop, exit code, startup validation failure |
| DI | singleton/scoped/transient, constructor injection, optional/required service, duplicate service, shutdown resolve failure |
| configuration | JSON/env/CLI merge, environment profile, typed binding, required value missing, validation failure |
| HTTP routing | `GET/POST/PUT/DELETE`, duplicate route, unknown route `404`, method mismatch `405`, route parameter binding, query binding |
| HTTP JSON | valid body, invalid JSON `400`, unsupported content type, response serialization, standard error response |
| HTTP DI | route handler constructor injection, request scope disposal, scoped service isolation across requests |
| HTTP lifecycle | accept loop start/stop, in-flight request drain, request during shutdown `503`, worker join |
| HTTP to zlink | HTTP handler가 `request_client_t`로 channel request를 보내고 response DTO를 반환 |
| zlink channel request/reply | client/server request, typed reply, timeout, handler not found, payload decode failure, reply serialization failure, disconnected peer |
| zlink channel send/event | fire-and-forget send, typed event dispatch, handler exception masking, no-reply path, queue full rejection |
| zlink pub/sub | publisher/subscriber delivery, multiple subscribers, unsubscribe/close cleanup, topic mismatch, disconnected subscriber |
| zlink route channel | manual connection, discovery connection, routing id selection, route handler dispatch, route handler not found, ambiguous route validation |
| zlink backpressure | pending request limit, outbound queue limit, send-ready resume, shutdown while pending, coroutine 동일 error kind |
| zlink serializer/codec | raw message, JSON DTO, optional MessagePack/Protobuf target off/on, serializer missing startup failure, invalid payload runtime failure |
| zlink lifecycle | channel bind/connect start order, receive loop start/stop, in-flight drain, shutdown after close, reconnect/disconnect event |
| SPOT | activation, destroy, join, leave, actor handler, publish, request_to, route resolver, discovery-backed remote address |
| SPOT ordering | same user Spot packet/timer/subscription ordering, actor packet ordering, Entry Spot timer non-global serialization |
| SPOT timer | CAPI timer projection, tick metadata, skipped tick, overrun policy, cancel, handler exception monitoring |
| STREAM | packet session, connected/disconnected/error callback, header validation, reply, write backpressure, disconnect cleanup |
| STREAM ordering | same session callback serialization, invalid header drop, close during pending write, session-scoped service disposal |
| ActorGateway | session bind, local actor relay, remote actor relay, bound session push, actor lookup, actor generation round-trip |
| ActorGateway failure | duplicate actor, type mismatch, missing actor, disconnected bound session, relay timeout, cleanup after disconnect |
| Registry/discovery | Spot remote address lookup, duplicate resolver rejection, ambiguous route channel validation, snapshot diff interval |
| handler model | sync handler, coroutine handler, exception filter, logging filter, validation filter |
| logging | console/file/category, correlation id, HTTP request log, zlink message log |
| observability | health/readiness/liveness, metrics event, trace/correlation, queue depth |
| error mapping | framework error to HTTP status, zlink error to framework error, internal exception masking |
| auth extension | auth filter registration, token extraction hook, current user context propagation |
| scheduling | hosted service, periodic timer, delayed task, graceful stop |
| developer tooling | CMake presets, vcpkg manifest, install consumer, CLion/Visual Studio configure smoke |
| samples | Bingo e2e, TicTacToe HTTP `POST /games` + zlink channel + STREAM connector e2e, server/client file log assertions |

zlink 관련 회귀 테스트는 `.NET` framework와 같은 기능 기대값을 기준으로 한다. C++ 서버
framework는 `co_await async()` 표면을 사용하지만, timeout, decode failure,
handler not found, shutdown, queue full, disconnected 같은 error kind와 로그/monitoring event는
같은 의미로 고정한다. 테스트는 단순히 process exit code만 확인하지 않고, request sequence,
topic/packet name, correlation id, server-side file log, client-side 결과를 함께 검증해야 한다.

CTest label은 최소 아래처럼 나눈다.

- `framework-contract`
- `framework-unit`
- `framework-integration`
- `framework-http`
- `framework-zlink`
- `framework-zlink-channel`
- `framework-zlink-spot`
- `framework-zlink-stream`
- `framework-zlink-actor-gateway`
- `framework-zlink-registry`
- `framework-host`
- `framework-config`
- `framework-observability`
- `framework-sample-smoke`
- `framework-sample-e2e`
- `framework-package`

샘플 회귀 테스트는 실행 파일 성공만 보면 안 된다. HTTP request, zlink channel request,
STREAM connector request, notification callback, server-side log를 모두 확인해야 한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: Draft -- ZLink Framework C++ Policy](../internals/cpp-framework-policy.ko.md) | [다음: Spec -- ZLink Framework C++ Interface Design](cpp-framework-interfaces.ko.md)
<!-- framework-adapter-nav:bottom:end -->
