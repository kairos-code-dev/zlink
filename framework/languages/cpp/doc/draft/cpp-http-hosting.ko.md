<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink HTTP Client For C++](./cpp-http-client.ko.md) | [다음: Draft -- ZLink Framework C++ Monitoring](./cpp-monitoring.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md) | [C++ 정책](./cpp-framework-policy.ko.md) | [Application Framework](./cpp-application-framework.ko.md) | [Framework 인터페이스](./cpp-framework-interfaces.ko.md) | [HTTP Client](./cpp-http-client.ko.md)

# Draft -- ZLink Framework C++ HTTP Hosting

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` framework가 ASP.NET Core Minimal API 기반 HTTP hosting을
> 어떻게 같은 의미로 제공할지 정리한다.

## 1. 기준 동작

기준은 `.NET` TicTacToe sample의 HTTP 흐름이다.

```text
HTTP client
  POST /games
      |
      v
ASP.NET Core Minimal API
  app.MapPost("/games", CreateGameHttpHandler.HandleAsync)
      |
      v
DI handler
  CreateGameHttpHandler.HandleAsync(
    CreateGameHttpReq request,
    IZLinkChannelClient client,
    ILoggerFactory loggerFactory,
    CancellationToken cancellationToken)
      |
      v
ZLink channel request
  client.RequestToChannel(SampleChannels.Play, new CreateGameReq(...))
      |
      v
HTTP JSON response
  Results.Ok(new CreateGameHttpRes(...))
```

C++ framework는 이 흐름을 아래 의미로 맞춘다.

- HTTP server는 app lifecycle에 묶인 hosted service다.
- HTTP route는 `MapGet`, `MapPost`, `MapPut`, `MapDelete`처럼 method와 path로 등록한다.
- HTTP endpoint는 `http://`와 `https://`를 모두 지원한다.
- request body는 JSON DTO로 변환한다.
- route handler는 DI에서 resolve한다.
- handler는 `request_client_t` 또는 `channel_client_t`를 주입받아 zlink channel에 요청한다.
- handler 반환 DTO는 JSON response body가 된다.
- 정상 응답은 기본 `200 OK`다.
- payload decode 실패는 `400 Bad Request`다.
- route 없음은 `404 Not Found`다.
- handler 또는 zlink request 실패는 error kind에 따라 HTTP status로 매핑한다.
- app shutdown은 HTTP accept loop와 진행 중 request dispatch를 닫는다.

HTTP hosting은 framework core 기능이다. 다만 목표는 MVC/view 중심 Web framework가 아니라
ASP.NET Core Minimal API처럼 route handler, DI, JSON binding, middleware/filter,
configuration, logging, zlink messaging을 한 application host 안에서 연결하는 것이다.

## 2. Public API

일반 application은 `app.add_zlink_framework(...)` 안에서 HTTP endpoint와 route를 등록한다.

```cpp
auto app = zlink::framework::app_t::create();

app.add_zlink_framework([&](auto &options) {
    options.discovery().add(topology.registry_router_endpoint);
    options.codecs().add_json();

    options.client_server_channel(sample_names_t::api_channel)
      .server(topology.api_channel_endpoint)
      .handler_group("api");
    options.client_server_channel(sample_names_t::play_channel)
      .client();

    options.http()
      .listen(topology.api_http_endpoint)
      .map_post<create_game_http_handler_t>("/games");

    options.handlers()
      .add<authenticate_player_handler_t>("api");
});
```

`options.http()`는 낮은 수준 `Boost.Beast`, socket, acceptor, thread, executor 타입을
노출하지 않는다.

## 3. Handler Shape

HTTP handler는 channel handler와 같은 방식으로 DTO와 DI 의존성을 선언한다.

```cpp
struct create_game_http_req_t {
    static constexpr const char *packet_name = "CreateGameHttpReq";
    std::string game_name;
};

struct create_game_http_res_t {
    static constexpr const char *packet_name = "CreateGameHttpRes";
    std::string game_id;
    std::string play_endpoint;
    std::string game_name;
};

class create_game_http_handler_t {
public:
    using request_type = create_game_http_req_t;
    using reply_type = create_game_http_res_t;
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::request_client_t>;

    explicit create_game_http_handler_t(
      zlink::framework::request_client_t &client);

    zlink::framework::task_t<create_game_http_res_t> handle(
      const create_game_http_req_t &request);
};
```

handler의 `handle(...)`은 sync 반환과 `task_t<T>` 반환을 모두 허용한다. C++ HTTP handler는
`.NET`의 `Task<IResult>` 또는 `Task<T>` 의미를 `task_t<T>`로 투영한다.

```cpp
task_t<create_game_http_res_t>
create_game_http_handler_t::handle(const create_game_http_req_t &request)
{
    auto created = co_await _client
      .request<create_game_res_t>(
        sample_names_t::play_channel,
        create_game_req_t { request.game_name })
      .submit();

    co_return create_game_http_res_t {
      created.game_id,
      created.play_endpoint,
      created.game_name
    };
}
```

`request_type`, `reply_type`, `dependency_types`, `handle(...)` 규칙은 message handler와
같게 유지한다. HTTP만 별도 생성자 주입 규칙을 만들지 않는다.

## 4. Route Builder

지원 범위는 typed JSON route다. `GET`, `POST`, `PUT`, `DELETE`를 같은 규칙으로
등록할 수 있어야 한다.

```cpp
namespace zlink::framework {

class http_options_builder_t {
public:
    http_options_builder_t &listen(std::string endpoint);
    http_options_builder_t &tls(
      std::function<void(http_tls_options_builder_t &)> configure);

    template <typename THandler>
    http_options_builder_t &map_get(std::string path);
    template <typename THandler>
    http_options_builder_t &map_post(std::string path);
    template <typename THandler>
    http_options_builder_t &map_put(std::string path);
    template <typename THandler>
    http_options_builder_t &map_delete(std::string path);

    template <typename TMiddleware>
    http_options_builder_t &use();

    http_options_builder_t &map_health(std::string path);
    http_options_builder_t &map_readiness(std::string path);
    http_options_builder_t &map_liveness(std::string path);
};

struct http_context_t {
    http_method_t method;
    std::string path;
    std::string correlation_id;
    std::map<std::string, std::string> request_headers;
    std::map<std::string, std::string> response_headers;
    std::optional<std::string> response_body;
    int response_status;

    http_context_t &response_header(std::string name, std::string value);
    http_context_t &json_response(int status, std::string body);
};

} // namespace zlink::framework
```

`listen(...)` endpoint는 `http://host:port`와 `https://host:port` 형식을 사용한다.

- `http://127.0.0.1:18080`
- `http://0.0.0.0:18080`
- `http://[::1]:18080`
- `https://127.0.0.1:18443`
- `https://0.0.0.0:18443`
- `https://[::1]:18443`

`https://` endpoint를 사용할 때는 TLS server certificate와 private key를 함께 설정해야
한다. TLS는 HTTP hosting의 transport option이고, zlink channel transport 설정과 섞어
설명하지 않는다.

```cpp
options.http()
  .listen("https://0.0.0.0:8443")
  .tls([](auto &tls) {
      tls.certificate_file("certs/server.crt")
        .private_key_file("certs/server.key");
  })
  .map_post<create_game_http_handler_t>("/games");
```

`map_post<THandler>(path)`는 아래 작업을 한 번에 수행한다.

- `THandler`를 service collection에 등록한다.
- `THandler::request_type`과 `THandler::reply_type`의 JSON serializer를 등록한다.
- method와 path를 route table에 등록한다.
- request마다 DI scope를 만들고 `THandler`를 resolve한다.
- `handle(...)`을 framework handler coroutine executor에서 실행한다.
- 결과 DTO를 JSON response body로 직렬화한다.
- handler가 `handle(request, http_context_t&)`를 제공하면 correlation id와 header 같은 HTTP
  문맥을 함께 받을 수 있다. `handle(request)`만 제공하는 기존 handler도 그대로 동작한다.

route parameter와 query string binding은 ASP.NET Core model binding을 단순화해서 따른다.

```cpp
options.http()
  .listen("http://0.0.0.0:8080")
  .map_get<get_game_http_handler_t>("/games/{gameId}");
```

handler request DTO에는 body, route, query에서 온 값이 합쳐진다. 충돌이 있으면
route parameter, query string, body 순서로 우선순위를 둔다. 이 우선순위는 startup
validation 문서와 테스트에서 고정한다.

`use<TMiddleware>()`는 `TMiddleware::before(http_context_t&)`와
`TMiddleware::after(http_context_t&)`를 route handler 앞뒤로 호출한다. middleware는 raw
Beast request나 socket을 받지 않고, `http_context_t`의 correlation id와 framework header
map만 사용한다. request에 `X-Correlation-Id` 또는 `X-Request-Id`가 있으면 그 값을 response
`X-Correlation-Id`로 되돌려 보내고, 없으면 runtime이 request correlation id를 만든다.
middleware가 `before(...)`에서 `json_response(status, body)`를 호출하면 HTTP runtime은
handler를 호출하지 않고 해당 JSON response를 반환한다. 이 short-circuit 경로도 `after(...)`
middleware를 거치므로 logging/correlation 처리를 한 곳에 둘 수 있다.

`map_health(path)`, `map_readiness(path)`, `map_liveness(path)`는 `app.health()` report를
HTTP JSON endpoint로 노출한다. readiness 또는 liveness가 `unhealthy`면 해당 endpoint는
`503 Service Unavailable`을 반환한다. 이 route는 사용자가 별도 handler를 만들지 않아도 되고,
health 집계 규칙은 `contracts/eventing/health.hpp`와 runtime diagnostics 구현이 소유한다.

## 5. Request / Response 계약

기본 typed route의 HTTP 계약은 아래와 같다.

| 항목 | 계약 |
|------|------|
| method | `GET`, `POST`, `PUT`, `DELETE` |
| scheme | `http` 또는 `https` |
| content type | `application/json` |
| request body | body가 있는 method에서는 `THandler::request_type` JSON |
| route parameter | `{name}` segment를 request DTO field에 binding |
| query string | `?name=value`를 request DTO field에 binding |
| response body | `THandler::reply_type` JSON |
| success status | `200 OK` |
| route not found | `404 Not Found` |
| method mismatch | `405 Method Not Allowed` |
| invalid JSON | `400 Bad Request` |
| serializer missing | startup validation 실패 |
| handler failure | error kind 기반 status mapping |

HTTP response에는 `Content-Type: application/json`을 기본으로 둔다. error response도 JSON
object로 반환한다.

```json
{
  "error": "payload_decode_failed",
  "message": "payload deserialization failed"
}
```

## 6. Error Mapping

framework error kind는 HTTP status로 매핑한다.

| framework error | HTTP status | 이유 |
|-----------------|-------------|------|
| `payload_decode_failed` | `400 Bad Request` | client body가 DTO로 변환되지 않았다 |
| `request_target_not_found` | `404 Not Found` | 대상 route, channel, service를 찾지 못했다 |
| `request_protocol_error` | `400 Bad Request` | request 의미가 framework 계약과 맞지 않는다 |
| `timeout` | `504 Gateway Timeout` | HTTP hosting 뒤의 zlink request가 시간 안에 끝나지 않았다 |
| `shutdown` | `503 Service Unavailable` | host가 종료 중이다 |
| `request_failed` | `500 Internal Server Error` | 내부 handler 또는 runtime 실패다 |

error kind가 명확하지 않은 예외는 `500 Internal Server Error`로 닫고 log/monitoring에
원인을 남긴다. HTTP client에는 C++ exception type 이름을 그대로 노출하지 않는다.

## 7. Lifecycle

HTTP server는 `hosted_service_t`로 실행한다.

- `app.run(...)`이 service provider를 만든 뒤 HTTP hosted service를 시작한다.
- HTTP accept loop는 background worker에서 동작한다.
- request dispatch는 framework handler coroutine executor 또는 HTTP runtime worker에서
  실행하되, public API에는 executor 타입을 노출하지 않는다.
- `app.stop()` 또는 signal shutdown이 들어오면 acceptor를 닫고 새 request를 받지 않는다.
- 진행 중 request는 graceful shutdown timeout 안에서 완료되도록 기다린다.
- shutdown 중 새 zlink submit을 무기한 만들지 않는다.

HTTP hosted service는 zlink channel runtime보다 뒤에 시작해야 한다. 그래야 `/games` 같은
HTTP request가 들어왔을 때 handler가 주입받은 channel client를 바로 사용할 수 있다.
정지는 반대로 HTTP server를 먼저 닫고 zlink runtime을 drain한다.

## 7.1 Middleware

HTTP middleware는 ASP.NET Core의 cross-cutting pipeline 개념을 C++ 형태로 투영한다.
초기 core 범위는 `options.http().use<TMiddleware>()`와 `http_context_t` 기반 before/after
hook이다. route별 filter 타입은 별도 public API로 두지 않고, middleware에서 method/path를
확인해 처리한다.

필수 축은 아래와 같다.

- exception filter
- logging filter
- validation filter
- auth filter
- correlation id filter
- custom middleware registration

middleware는 DI를 사용할 수 있다. public API에 `boost::beast` request/response를 노출하지
않고 framework의 `http_context_t` 같은 추상 타입을 사용한다. `http_context_t`는 request id,
method, path, header 조회, response status 설정, short-circuit JSON response 설정을 제공한다.

## 8. Runtime 구현 경계

public contract는 아래 위치에 둔다.

```text
framework/include/zlink/framework/contracts/http/http.hpp
framework/include/zlink/framework/http.hpp
```

runtime 구현은 아래 위치에 둔다.

```text
framework/src/runtime/http/http_hosted_service.cpp
framework/src/runtime/http/http_route_registry.cpp
framework/src/runtime/http/http_request_dispatcher.cpp
```

`Boost.Beast`, `Boost.Asio`, OpenSSL/SSL context 타입은 runtime 구현 파일에서만 사용한다.
public header에는 `boost::beast`, `boost::asio`, TCP socket, acceptor, request parser,
SSL stream, SSL context 타입이 나타나면 안 된다.

HTTP runtime은 zlink core CAPI timer나 dispatch pump를 직접 사용하지 않는다. HTTP는
framework host가 소유한 별도 ingress이고, zlink messaging으로 들어가는 지점에서
`request_client_t` 또는 `channel_client_t`를 사용한다.

## 9. ASP.NET Core Minimal API 대응표

| ASP.NET Core / .NET sample | C++ framework |
|----------------------------|---------------|
| `WebApplication.CreateBuilder()` | `app_t::create()` |
| `builder.WebHost.UseUrls(url)` | `options.http().listen(url)` |
| Kestrel HTTPS endpoint/certificate option | `options.http().listen("https://...").tls(...)` |
| `builder.Services.AddZLinkFramework(...)` | `app.add_zlink_framework(...)` |
| `app.MapPost("/games", Handler.HandleAsync)` | `options.http().map_post<handler_t>("/games")` |
| `app.MapGet("/games/{id}", ...)` | `options.http().map_get<handler_t>("/games/{id}")` |
| request body model binding | JSON serializer로 `request_type` 생성 |
| route/query model binding | route parameter와 query string을 `request_type`에 병합 |
| method parameter DI | `dependency_types` 기반 생성자 주입 |
| middleware/filter | `options.http().use<TMiddleware>()`와 `http_context_t` |
| `CancellationToken` | C++ host shutdown/drain 정책. public handler signature에는 기본 노출하지 않음 |
| `Results.Ok(dto)` | handler가 `reply_type` DTO 반환 |
| `HttpClient.PostAsJsonAsync(...)` | `zlink::http_client`가 JSON POST 수행 |
| `ReadFromJsonAsync<T>()` | `zlink::http_client`가 `message_t::parse_json<T>()` 사용 |

## 10. TicTacToe Sample 반영

C++ TicTacToe sample은 `.NET` TicTacToe와 같은 HTTP 시작 흐름을 가져야 한다.

필수 변경은 아래와 같다.

- `sample_topology_t`에 `api_http_endpoint`를 추가한다.
- `Server/Api` role은 zlink API channel server와 HTTP endpoint를 함께 구성한다.
- `CreateGameHttpReq`, `CreateGameHttpRes` DTO를 C++ shared contracts에 추가한다.
- `CreateGameHttpHandler`는 HTTP request를 받아 Play channel로 `CreateGameReq`를 보낸다.
- client는 `zlink::http_client`로 먼저 `POST /games`를 호출해 `game_id`, `play_endpoint`,
  `game_name`을 받는다.
- `api_http_endpoint`가 `https://`이면 client는 `zlink::http_client`의 TLS verification
  option을 명시해 같은 흐름을 검증한다.
- 이후 stream connector는 HTTP 응답의 `play_endpoint`로 연결한다.
- client smoke/e2e log는 HTTP request, API-to-Play channel request, stream connector request가
  모두 발생했는지 확인한다.

Bingo sample은 `.NET` Bingo가 HTTP entry를 사용하지 않으므로 HTTP path를 억지로 추가하지
않는다. Bingo는 기존 session stream 중심 검증을 유지한다.

## 11. 결정된 구현 기준

아래 항목은 구현 전에 따를 기준으로 닫았다.

| 항목 | 결정 |
|------|------|
| HTTP를 core framework에 둘지 extension에 둘지 | core framework에 둔다. `.NET` sample parity와 “이 framework 하나로 충분해야 한다”는 요구 때문이다 |
| 구현 라이브러리 | `Boost.Beast`를 runtime private dependency로 사용한다 |
| public API에 Beast/Asio 노출 여부 | 노출하지 않는다 |
| HTTPS/TLS 지원 | core HTTP hosting에서 지원한다. public API는 certificate/private key 설정만 노출하고 SSL 구현 타입은 숨긴다 |
| sample HTTP client HTTPS | `zlink::http_client`가 지원한다. test certificate trust는 client option으로 명시한다 |
| route matching | exact path와 `{name}` path parameter를 지원한다 |
| method 지원 | `GET`, `POST`, `PUT`, `DELETE`를 같은 builder 패턴으로 지원한다 |
| cancellation token | C++ public handler signature에는 별도 cancel token을 넣지 않는다. shutdown/drain과 timeout 정책으로 처리한다 |
| response customization | typed DTO는 `200 OK` 기본이다. status/header 직접 제어는 `http_response_t` 반환 handler로 확장한다 |

## 12. 회귀 테스트

최소 테스트는 아래 축으로 둔다.

- contract header compile: `#include <zlink/framework/http.hpp>`
- route registry: 같은 method/path 중복 등록은 startup validation 실패
- method별 route: `GET`, `POST`, `PUT`, `DELETE`
- scheme별 listen: `http://`, `https://`
- HTTPS TLS option: certificate/private key 누락은 startup validation 실패
- HTTPS loopback: test certificate로 JSON request/response 성공
- HTTP client contract: `#include <zlink/http_client.hpp>`가 framework runtime header를
  끌어오지 않는다
- HTTP client JSON: `zlink::http_client`가 typed DTO request를 JSON으로 보내고 reply DTO를
  `message_t::parse_json<T>()` 흐름으로 읽는다
- HTTP client HTTPS: test certificate trust 설정이 있으면 `https://` JSON request/response가
  성공한다
- HTTP client TLS failure: 신뢰하지 않은 certificate와 hostname mismatch는 명시적인
  client error로 실패한다
- HTTP client timeout/status mapping: timeout, `400`, `404`, `500` 응답이 client result/error
  kind로 고정된다
- HTTP handler e2e: `zlink::http_client`로 `GET`, `POST`, `PUT`, `DELETE` route를 호출해
  DTO binding, DI handler 실행, JSON response, status mapping을 검증한다
- route parameter binding: `/games/{gameId}`가 DTO field로 들어간다
- query string binding: `?page=1`이 DTO field로 들어간다
- body/route/query merge 우선순위 고정
- JSON binding: request body를 DTO로 변환하고 reply DTO를 JSON으로 반환
- DI: HTTP handler가 `request_client_t`를 생성자 주입으로 받는다
- middleware/filter: logging, exception, validation filter 순서와 short-circuit
- error mapping: invalid JSON은 `400`, unknown route는 `404`, timeout은 `504`
- lifecycle: `app.stop()`이 HTTP accept loop를 닫고 worker thread를 join한다
- TicTacToe sample e2e: client가 `POST /games` 뒤 stream connector로 게임을 진행한다
