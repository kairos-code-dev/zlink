<!-- framework-adapter-nav:start -->
[스펙 목차](README.ko.md) | [이전: C++ HTTP Hosting](60-http-hosting.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)


# Spec -- ZLink Framework C++ Embedded HTTP Server

> 이 문서는 `C++` framework 안에서 제공하는 내장 HTTP 웹서버 runtime의
> 정식 계약을 정리한다.
>
> [HTTP Hosting](60-http-hosting.ko.md)이 사용자가 보는 route, handler, DTO binding
> 표면을 다룬다면, 이 문서는 그 표면 뒤에서 동작하는 server runtime, connection lifecycle,
> timeout, TLS, shutdown, observability, 성능 기준을 다룬다.

## 1. 결정

`ZLink Framework for C++`는 backend API framework로 쓰일 수 있어야 한다. 기능 수준은
`Drogon`, `Oat++` 같은 C++ backend API framework에서 사용자가 자연스럽게 기대하는 범위를
기준선으로 삼는다. public application model은 route mapping, typed DTO binding,
middleware와 lifecycle을 C++ 스타일로 제공한다.

따라서 core는 외부 full web framework 위에 올라가지 않는다. HTTP server는 zlink framework가
소유하는 내장 runtime으로 제공하고, 구현 기반은 `Boost.Beast`와 `Boost.Asio`를 private
implementation으로 사용한다.

핵심 결정은 아래와 같다.

- public API는 zlink가 소유한다.
- route mapping은 `map_get`, `map_post`, `map_put`, `map_delete`처럼 HTTP method가 드러나는
  `map_get`, `map_post`, `map_put`, `map_delete`를 사용한다.
- server endpoint는 C++ 사용자에게 직관적인 `listen(...)`으로 표현한다.
- `Boost.Beast`, `Boost.Asio`, OpenSSL stream, socket, acceptor 타입은 public header에
  노출하지 않는다.
- `Drogon`, `Oat++`, `Pistache`, `cpp-httplib`는 core dependency가 아니라 기능과 성능을
  검토할 때의 참고 기준으로만 둔다.
- 1차 내장 server 목표는 HTTP/1.1 backend API server다.
- 성능 목표는 단순 동작이 아니라 `Drogon`, `Oat++` 같은 C++ backend API framework에
  견줄 수 있는 고성능 내장 server다.
- HTTP/2, HTTP/3, WebSocket, static file server, template engine, ORM은 core 1차 목표가 아니다.

## 2. 왜 내장 웹서버가 필요한가

HTTP handler는 application message를 처리한다. 반면 웹서버는 handler 앞뒤의 HTTP transport와
protocol lifecycle을 처리한다.

```text
+---------------------------+
| Client                    |
+-------------+-------------+
              |
              v
+-------------+-------------+
| Embedded HTTP Server      |
| listen / accept / parse   |
| route / timeout / limits  |
| response / shutdown       |
+-------------+-------------+
              |
              v
+-------------+-------------+
| ZLink HTTP Hosting        |
| DTO bind / DI / middleware|
| handler invocation        |
+-------------+-------------+
              |
              v
+-------------+-------------+
| Application Handler       |
| business logic / zlink IO |
+---------------------------+
```

사용자는 아래 수준의 코드만 보아야 한다.

```cpp
app.add_zlink_framework ([&] (auto &options) {
    options.http ()
      .listen (topology.api_http_endpoint)
      .map_post<create_game_http_handler_t> ("/games");
});
```

이 코드 뒤에서 framework는 TCP accept, HTTP parser, TLS handshake, request timeout,
route matching, DI scope, middleware, response write, graceful shutdown을 처리해야 한다.

## 3. 현재 상태

현재 C++ framework에는 `runtime::http_host_service_t`가 있다. `options.http().listen(...)`으로
endpoint가 등록되면 `app_t::add_zlink_framework(...)`가 이 service를 hosted service로 추가한다.

현재 runtime이 이미 수행하는 일은 아래와 같다.

- `http://`, `https://` endpoint parse
- TCP bind/listen/accept
- `Boost.Beast` 기반 HTTP request read
- SSL 빌드와 TLS 설정이 있는 경우 HTTPS endpoint의 TLS handshake
- connection당 request loop와 keep-alive
- request header/body/write/keep-alive timeout
- request body size limit와 header size limit
- max connections 한도와 overload 처리
- HTTP method와 path 기반 route matching
- path parameter, query parameter binding
- health/readiness/liveness route
- request별 DI scope 생성
- middleware before/after 실행
- handler coroutine executor 호출
- framework error kind를 HTTP status와 JSON error body로 매핑
- HTTP response write
- `stop()`에서 acceptor close, 진행 중 request drain, open socket 정리, thread join

다만 현재 구현은 “최소 내장 HTTP host”에 가깝다. backend API framework의 기본 server로
보기 위해서는 아래 기능이 추가되어야 한다.

- connection/request observability extension point
- request logging과 correlation id의 표준화
- malformed request에 대한 `400 Bad Request`
- server option startup validation

## 4. 목표 포지션

목표는 “Kestrel과 같은 위치의 zlink embedded HTTP server”다. 이 표현은 Kestrel과 모든
기능과 성능이 즉시 동일하다는 뜻이 아니다. 의미는 아래와 같다.

- application host 안에 내장된다.
- route handler, DI, logging, configuration, health와 같은 framework 표면에 연결된다.
- 별도 외부 웹 프레임워크를 사용자가 배워야 하지 않는다.
- reverse proxy 뒤에서도 동작하고, 단독 backend API server로도 실행할 수 있다.
- 운영 환경에서 필요한 timeout, limit, shutdown, observability 옵션을 제공한다.

`Drogon`과 `Oat++`는 기능과 성능 기준선이다. zlink framework는 그 수준의 backend API server
사용성과 처리량을 목표로 하지만, public API는 zlink와 `.NET` 모델을 따른다.

## 5. Drogon / Oat++ 분석과 설계 반영

`Drogon`과 `Oat++`를 그대로 가져오지 않는 이유는 public application model을 zlink가 소유해야
하기 때문이다. 그러나 두 framework가 고성능을 얻는 방식은 내장 HTTP server 설계에 반영한다.

분석 기준은 공식 문서와 repository에서 확인할 수 있는 public architecture, server 구성 요소,
benchmark 흐름이다. 내부 구현 세부를 그대로 복제하는 것이 아니라, 고성능 server에서 반복되는
구조를 zlink runtime 설계 원칙으로 바꾼다.

- Drogon은 비동기 I/O와 event loop 기반 HTTP 처리에 초점을 둔다. request handler는 callback,
  coroutine, async task로 분리할 수 있고, network runtime은 blocking handler에 묶이지 않도록
  설계되어 있다.
- Drogon의 강점은 thread마다 event loop를 두고 connection을 그 loop에 붙여 lock 경쟁을 줄이는
  구조다. HTTP route, filter, plugin, database client도 같은 비동기 모델에 연결된다.
- Oat++는 동기 server와 async server를 구분한다. 고동시성 목표에서는 async server가 connection
  수를 많이 유지할 수 있도록 I/O, timer, worker executor를 분리한다.
- Oat++는 router/controller model, connection provider, connection handler를 나눠 HTTP server
  구성 요소의 책임을 분리한다. 사용자는 controller endpoint를 보지만, runtime은 연결 수명과
  request parsing을 숨긴다.
- 두 framework 모두 keep-alive, async request 처리, route dispatch, middleware/filter에 해당하는
  extension point, JSON binding을 application model 뒤에 숨긴다.

zlink가 받아들일 설계 원칙:

| 기준 | Drogon/Oat++에서 본 형태 | zlink 반영 |
|------|--------------------------|------------|
| I/O model | event loop와 non-blocking request 처리 | 1차 구현은 bounded worker pool로 connection I/O를 제한하고, perf 고도화에서 async read/write로 전환한다 |
| connection ownership | connection은 특정 worker에 붙는다 | connection은 worker pool에서 처리하고 connection당 thread를 만들지 않는다 |
| handler isolation | handler가 network loop를 오래 막지 않는다 | I/O executor와 handler executor를 분리한다 |
| route dispatch | route metadata를 runtime 시작 전에 준비한다 | route table은 startup에서 compile한다 |
| buffer lifecycle | connection/request buffer를 재사용한다 | Beast buffer, parser, response serializer 재사용 정책을 둔다 |
| overload control | connection 수와 timeout으로 부하를 제한한다 | connection, timeout, size limit를 둔다 |
| observability | request duration/status/connection 상태를 기록한다 | logging은 app model에 연결하고 metrics는 별도 확장 gate로 둔다 |
| public surface | framework 타입을 앞세우고 socket 타입은 숨긴다 | Beast/Asio/OpenSSL 타입은 public header에 노출하지 않는다 |

따라서 zlink 내장 server는 아래 구조를 목표로 한다.

```text
+---------------------------+
| Listener Executor Pool    |
| accept / endpoint setup   |
+-------------+-------------+
              |
              v
+-------------+-------------+
| Connection Executor       |
| read / parse / write      |
| TLS / keep-alive / timer  |
+-------------+-------------+
              |
              v
+-------------+-------------+
| HTTP Request Pipeline     |
| route / bind / middleware |
+-------------+-------------+
              |
              v
+-------------+-------------+
| Framework Executor        |
| DI / handler / zlink call |
+---------------------------+
```

I/O worker는 socket read/write와 timeout을 처리한다. handler가 오래 걸리거나 zlink channel
request를 기다리는 경우에도 connection마다 OS thread를 새로 만들면 안 된다. handler 실행은
framework executor로 넘기고, response write는 connection I/O 흐름에서 수행한다.

connection별 state는 mutex를 많이 쓰는 shared object가 아니라 connection 처리 흐름 안에서만
갱신되는 object로 둔다. active connection registry는 shutdown, metrics, max connection 판단에
필요한 최소 정보만 가진다.

TLS context 생성은 listener start 시점에 수행하지만, TLS handshake는 accepted connection의
connection executor에서 timeout과 함께 처리한다. 이렇게 나누면 endpoint 설정과 connection
lifecycle이 섞이지 않는다.

## 6. 고성능 설계 기준

고성능 목표는 benchmark 단계에서 갑자기 맞출 수 없다. runtime 구조가 처음부터 아래 조건을
만족해야 한다.

### 6.1 Bounded I/O worker 구조

- connection I/O는 bounded worker pool 또는 non-blocking async state machine으로 제한한다.
- connection당 OS thread를 만들지 않는다.
- accept/read/write 실행은 perf gate와 worker 비점유 계약을 만족해야 한다.
- blocking handler, serializer, logging sink가 I/O executor를 막지 않도록 분리한다.
- connection 내부 request loop는 직렬 실행으로 단순화한다.
- cross-thread wakeup은 request 완료, shutdown, timeout 같은 필요한 지점으로 제한한다.
- thread 수 기본값은 CPU core 수를 기준으로 잡되, public API에는 executor 세부 타입을 노출하지
  않는다.

### 6.2 Allocation 과 buffer 관리

- request마다 TLS context, route table, serializer registry를 만들지 않는다.
- endpoint별 TLS context와 compiled route table은 startup에서 한 번 만든다.
- connection buffer는 keep-alive request 사이에서 재사용한다.
- response body는 가능한 한 size를 예측해 reserve한다.
- error response JSON은 hot path에서 매번 복잡한 object graph를 만들지 않도록 작은 formatter로
  처리한다.
- metrics counter는 lock-free 또는 shard counter를 우선 검토한다. request마다 global mutex를
  잡는 구조는 금지한다.

### 6.3 Route dispatch

- route path는 startup에서 segment 형태로 compile한다.
- static route, parameter route, wildcard route를 분리해 match 순서를 고정한다.
- request target parsing은 path와 query를 한 번만 분리한다.
- route match 실패, method mismatch, content type mismatch는 handler executor로 넘기지 않고
  HTTP pipeline에서 바로 response를 만든다.

### 6.4 Backpressure 와 overload

- max connection을 넘으면 accept 직후 overload response를 쓰거나 connection을 닫는다.
- max in-flight request 수를 둬 handler executor queue가 무한히 커지지 않게 한다.
- write queue 크기를 제한한다. 느린 client가 response buffer를 무한히 점유하면 안 된다.
- body/header limit는 read 중에 적용한다. body 전체를 읽은 뒤에야 초과를 판단하면 안 된다.
- graceful shutdown 중에는 새 connection과 새 keep-alive request를 받지 않는다.

### 6.5 JSON 과 handler 경계

- JSON parse는 request DTO binding에서 한 번만 수행한다.
- DTO serialize는 response write 직전에 한 번만 수행한다.
- handler가 `std::string` JSON을 직접 만들도록 요구하지 않는다. 필요하면 `http_response_t` 같은
  명시적 escape hatch를 제공한다.
- zlink channel request를 호출하는 handler는 HTTP server benchmark와 별도 end-to-end benchmark로
  분리한다. HTTP runtime 자체의 병목과 zlink messaging 비용을 섞어 해석하지 않는다.

### 6.6 Logging 과 metrics 비용

- request log는 handler 결과가 확정된 뒤 한 번 남긴다.
- access log formatting이 hot path에서 과도한 allocation을 만들면 안 된다.
- metrics는 status code, route template, method 같은 낮은 cardinality label만 기본으로 둔다.
- remote address, request id 같은 높은 cardinality 값은 metric label이 아니라 log field로 둔다.

### 6.7 금지 설계

- connection당 thread를 기본 model로 삼지 않는다.
- request마다 route table, TLS context, service provider root를 만들지 않는다.
- I/O executor에서 user handler를 직접 오래 실행하지 않는다.
- public option으로 Beast parser, Asio executor, OpenSSL context를 직접 받지 않는다.
- benchmark를 위해 public API를 우회하는 private fast path를 만들지 않는다.

## 7. Public API 원칙

HTTP server API는 route handler API와 섞이되, 내부 server 구현 타입은 숨긴다.

```cpp
app.add_zlink_framework ([&] (auto &options) {
    options.http ()
      .listen ("https://0.0.0.0:8443")
      .configure_tls ([] (auto &tls) {
          tls.certificate_file ("cert.pem")
             .private_key_file ("key.pem");
      })
      .configure_server ([] (auto &server) {
          server.set_max_connections (4096)
                .set_max_request_body_size (1024 * 1024)
                .set_request_headers_timeout (std::chrono::seconds (15))
                .set_keep_alive_timeout (std::chrono::seconds (60));
      })
      .map_get<get_game_handler_t> ("/games/{id}")
      .map_post<create_game_http_handler_t> ("/games")
      .map_put<update_game_handler_t> ("/games/{id}")
      .map_delete<delete_game_handler_t> ("/games/{id}");
});
```

위 예시는 embedded HTTP server option까지 포함한 확정 public API 방향이다. `map_get`,
`map_post`, `map_put`, `map_delete`, `listen`, `configure_tls`, `configure_server`는
[framework option builder naming](../../../../../../../doc/principal/framework-option-builder-naming.ko.md)
원칙을 따른다.

이 표면은 아래 규칙을 따른다.

- C++ public method는 `snake_case`를 사용한다.
- route mapping은 `.NET` Minimal API 개념을 따른다.
- endpoint listen 설정은 C++ 서버 구성으로 읽히게 `listen(...)`을 사용한다.
- TLS 설정 영역은 `configure_tls(...)`로 연다.
- TLS 설정은 마지막 `listen(...)` endpoint에 적용되거나, endpoint builder를 반환하는 방식으로
  명확히 묶는다.
- server runtime 설정 영역은 `configure_server(...)`로 연다.
- server option은 route handler가 아니라 HTTP server runtime에 적용된다.
- logger, DI, serializer, monitoring은 별도 framework 표면과 연결되며 HTTP server가 자체
  독립 framework처럼 노출하지 않는다.

## 8. 내부 구조

내장 server runtime은 최소한 아래 component로 분리한다.

```text
+---------------------------+
| http_host_service_t       |
| hosted service lifecycle  |
+-------------+-------------+
              |
              v
+-------------+-------------+
| http_server_t             |
| endpoint set / options    |
| shutdown coordinator      |
+-------------+-------------+
              |
              v
+-------------+-------------+
| http_listener_t           |
| accept loop / TLS context |
+-------------+-------------+
              |
              v
+-------------+-------------+
| http_connection_t         |
| read loop / keep-alive    |
| timeout / write response  |
+-------------+-------------+
              |
              v
+-------------+-------------+
| http_request_pipeline_t   |
| route / middleware / DI   |
+---------------------------+
```

역할은 아래처럼 나눈다.

| component | 책임 |
|-----------|------|
| `http_host_service_t` | app hosted service entry point. start/stop과 service provider 연결 |
| `http_server_t` | endpoint snapshot, active connection registry, graceful shutdown coordinator |
| `http_listener_t` | endpoint별 bind/listen/accept, TLS context 초기화 |
| `http_connection_t` | connection별 request loop, timeout, keep-alive, response write |
| `http_request_pipeline_t` | route matching, DI scope, middleware, handler invocation |
| `http_error_mapper_t` | framework error와 HTTP status/body mapping |
| `http_server_metrics_t` | accepted/active/rejected/request duration/status counters |

위 component는 runtime의 필수 책임 경계다. public 계약 타입(`http_request_t`,
`http_response_t`, `http_context_t`, `http_*_options_builder_t`)은
`contracts/http/http.hpp`에 둔다.

이 분리는 POSD 관점에서 중요하다. accept loop, TLS, timeout, route invocation을 한 class에 모두
넣으면 shallow module이 된다. public API는 단순하지만, runtime 내부는 변경 이유별로 나뉘어야 한다.

## 9. Endpoint 와 TLS

endpoint는 `http://host:port` 또는 `https://host:port` 형식이다. port를 생략하면 scheme에 따라
기본 port(`http` 80, `https` 443)를 채운다. host가 없으면 startup validation에서 실패한다.

TLS는 endpoint별 설정이다. HTTPS endpoint에는 certificate와 private key가 필요하다. TLS 설정이
없는 HTTPS endpoint는 runtime start 뒤가 아니라 options apply 또는 hosted service start 전에
실패해야 한다.

TLS runtime 기준:

- certificate/private key 파일 존재 여부를 startup에서 확인한다.
- TLS context는 request마다 만들지 않고 listener start 시점에 한 번 만든다.
- TLS handshake timeout을 둔다.
- TLS handshake 실패는 request handler 오류가 아니라 connection 오류로 집계한다.
- public API는 OpenSSL 또는 Asio SSL 타입을 노출하지 않는다.

## 10. Accept 와 Connection 모델

connection당 thread 모델을 사용하지 않는다. bounded worker pool이나 async
accept/read/write state machine으로 connection I/O를 처리하고 고성능 perf gate를
안정적으로 통과해야 한다.

목표 모델:

- endpoint별 listener는 accept를 반복하고 accepted connection을 bounded worker pool로 넘긴다.
- accepted connection은 `http_connection_t`로 넘긴다.
- connection은 request loop를 가진다.
- keep-alive가 켜진 HTTP/1.1 connection은 여러 request를 처리한다.
- shutdown 시작 뒤에는 새 connection을 받지 않는다.
- shutdown drain 동안 active request는 timeout 안에서 완료를 기다린다.
- drain timeout이 지나면 connection을 닫는다.

구현은 component 분리, TLS context 재사용, keep-alive loop, timeout, limit, metrics와
stress test를 모두 제공해야 한다. accept/read/write 실행 모델은 baseline 성능 gate와
worker 비점유 계약을 만족해야 하며, blocking 구조로 이를 만족하지 못하면 async state
machine을 사용한다.

## 11. Request 처리 흐름

request 처리의 표준 흐름은 아래와 같다.

```text
Read request
  -> validate method / target / headers
  -> create context
  -> match system route
  -> match user route
  -> create DI scope
  -> run middleware before
  -> bind body / route / query
  -> invoke handler
  -> run middleware after
  -> map response
  -> write response
```

중요한 규칙:

- malformed request는 `400 Bad Request`로 닫는다.
- 지원하지 않는 method는 `405 Method Not Allowed`로 닫는다.
- route가 없으면 `404 Not Found`로 닫는다.
- content type이 맞지 않으면 `request_protocol_error`를 던지고 `400 Bad Request`로 닫는다.
- body size가 limit를 넘으면 `413 Payload Too Large`를 사용한다.
- handler timeout은 `504 Gateway Timeout`을 사용한다.
- shutdown 중 새 request는 `503 Service Unavailable`로 닫거나 connection을 drain 정책에 따라 닫는다.
- framework exception은 `framework_error_kind_t` 기반으로 status와 JSON body를 만든다.

## 12. Binding 과 Handler 통합

HTTP server는 handler model을 새로 만들지 않는다. `cpp-http-hosting.ko.md`의 handler shape를
그대로 사용한다.

```cpp
class create_game_http_handler_t {
  public:
    using request_type = create_game_http_req_t;
    using reply_type = create_game_http_res_t;
    using dependency_types =
      zlink::framework::dependency_list_t<
        zlink::framework::channel_client_t,
        zlink::framework::logger_t<create_game_http_handler_t>>;

    task_t<create_game_http_res_t> handle (const create_game_http_req_t &request);
};
```

server runtime은 typed route에서 아래 작업을 담당한다.

- route path와 query를 request binding input에 합친다.
- JSON body를 request DTO로 deserialize한다.
- request DI scope를 만든다.
- handler를 framework DI에서 resolve한다.
- handler 결과 DTO를 JSON body로 serialize한다.

server runtime은 raw route에서 아래 작업을 담당한다.

- `http_request_t`를 만든다.
- method, path, target, route value, query value, header, body, content type, remote endpoint를
  public framework type으로 복사한다.
- handler를 framework DI에서 resolve한다.
- handler가 반환한 `http_response_t`의 status, header, content type, body를 HTTP response로 쓴다.

지원해야 하는 handler shape:

- `reply_type handle(const request_type &request)`
- `task_t<reply_type> handle(const request_type &request)`
- `reply_type handle(const request_type &request, http_context_t &context)`
- `task_t<reply_type> handle(const request_type &request, http_context_t &context)`
- `reply_type handle(const request_type &request, const http_request_t &http)`
- `task_t<reply_type> handle(const request_type &request, const http_request_t &http)`
- `reply_type handle(const request_type &request, const http_request_t &http, http_context_t &context)`
- `task_t<reply_type> handle(const request_type &request, const http_request_t &http, http_context_t &context)`
- `http_response_t handle(const request_type &request)`
- `http_response_t handle(const request_type &request, http_context_t &context)`
- `task_t<http_response_t> handle(const request_type &request)`
- `task_t<http_response_t> handle(const request_type &request, http_context_t &context)`
- `http_response_t handle(const request_type &request, const http_request_t &http)`
- `task_t<http_response_t> handle(const request_type &request, const http_request_t &http)`
- `http_response_t handle(const request_type &request, const http_request_t &http, http_context_t &context)`
- `task_t<http_response_t> handle(const request_type &request, const http_request_t &http, http_context_t &context)`
- `http_response_t handle(const http_request_t &request)`
- `task_t<http_response_t> handle(const http_request_t &request)`

handler는 socket, HTTP parser, Beast request, TLS stream을 알면 안 된다. HTTP 세부 정보가
필요하면 `http_request_t`, response 직접 제어가 필요하면 `http_response_t`를 사용한다.

## 13. Middleware, Filter, Error Boundary

middleware는 HTTP context를 다룬다. filter는 handler invocation과 message dispatch 정책을
다룬다. 둘을 같은 개념으로 섞지 않는다.

HTTP middleware 책임:

- correlation id
- request logging
- auth header 검사
- response header 추가
- short-circuit response
- CORS 같은 HTTP 전용 정책

handler/filter 책임:

- DTO validation
- handler exception masking
- zlink request failure mapping
- business level audit

middleware `after`는 handler 성공뿐 아니라 short-circuit, handler exception, binding failure
경로에서도 가능한 한 실행되어야 한다. 그래야 logging/correlation 지식이 handler마다 반복되지 않는다.

## 14. Server Options

server option은 endpoint 전체 또는 HTTP server 전체에 적용된다. route handler마다 같은 option을
반복하게 만들지 않는다.

1차 public option:

| option | 기본값 | 의미 |
|--------|--------|------|
| `max_connections` | 구현 기본값 | 동시에 유지할 active connection 수 |
| `max_request_body_size` | 1 MiB 또는 정책값 | JSON body 최대 크기 |
| `max_header_size` | 정책값 | header 전체 크기 제한 |
| `request_headers_timeout` | 5s | header read 제한 시간 |
| `request_body_timeout` | 5s | body read 제한 시간 |
| `write_timeout` | 5s | response write 제한 시간 |
| `keep_alive_timeout` | 5s | keep-alive 연결에서 다음 request header를 기다리는 제한 시간 |
| `graceful_shutdown_timeout` | 5s | 종료 시 진행 중인 request가 끝나기를 기다리는 시간 |
| `max_keep_alive_requests` | 100 또는 무제한 | connection당 request 수 제한 |

Public builder 이름은 아래처럼 고정한다.

```cpp
namespace zlink::framework {

class http_tls_options_builder_t {
public:
    http_tls_options_builder_t &certificate_file(std::string path);
    http_tls_options_builder_t &private_key_file(std::string path);
};

class http_server_options_builder_t {
public:
    http_server_options_builder_t &set_max_connections(std::size_t value);
    http_server_options_builder_t &set_max_request_body_size(std::size_t bytes);
    http_server_options_builder_t &set_max_header_size(std::size_t bytes);
    http_server_options_builder_t &set_request_headers_timeout(
      std::chrono::milliseconds value);
    http_server_options_builder_t &set_request_body_timeout(
      std::chrono::milliseconds value);
    http_server_options_builder_t &set_write_timeout(
      std::chrono::milliseconds value);
    http_server_options_builder_t &set_keep_alive_timeout(
      std::chrono::milliseconds value);
    http_server_options_builder_t &set_graceful_shutdown_timeout(
      std::chrono::milliseconds value);
    http_server_options_builder_t &set_max_keep_alive_requests(
      std::size_t value);
};

class http_options_builder_t {
public:
    http_options_builder_t &configure_tls(
      std::function<void(http_tls_options_builder_t &)> configure);

    http_options_builder_t &configure_server(
      std::function<void(http_server_options_builder_t &)> configure);
};

} // namespace zlink::framework
```

기본값은 문서만으로 확정하지 않는다. 구현 시 perf, e2e, 운영 기대치를 보고 정한다. 단, option의
의미와 실패 status는 먼저 고정한다. `configure_server(...)`는 명사형 `server(...)`보다 설정
영역을 연다는 의도가 분명하므로 이 이름을 사용한다.

## 15. Observability

내장 server는 framework logging, monitoring, health와 연결되어야 한다.

logging:

- request start/end log
- status code와 duration
- route template
- correlation id
- remote endpoint
- error kind

monitoring event:

- listener started/stopped
- connection accepted/closed
- request started/completed
- request rejected
- timeout
- TLS handshake failed
- graceful shutdown started/completed

metrics:

- active connections
- total accepted connections
- rejected connections
- in-flight requests
- request duration
- response status count
- request body bytes

health:

- listener bind 실패는 startup failure다.
- started listener 수와 expected endpoint 수가 다르면 unhealthy다.
- shutdown 중 readiness는 unhealthy로 바뀐다.

## 16. 보안과 운영 기준

내장 server는 backend API server이므로 기본 보안 경계를 제공해야 한다.

- default body limit를 둔다.
- header limit를 둔다.
- timeout 없는 request read를 허용하지 않는다.
- TLS certificate/private key 설정 오류를 시작 전에 잡는다.
- error response는 stack trace나 내부 파일 경로를 노출하지 않는다.
- reverse proxy 뒤에서 쓸 수 있도록 forwarded header 정책을 별도 option으로 둔다.
- request logging에서 민감 header를 그대로 기록하지 않는다.

auth provider 자체는 core 1차 목표가 아니다. core는 middleware/filter extension point와
header/context API를 제공한다.

## 17. 성능 기준

성능 목표는 [ZLink Framework Performance Policy](../../../../perf/README.ko.md)와
[C++ Framework Performance Plan](../../../../perf/bindings/cpp-framework-performance.ko.md)에
맞춘다. HTTP server perf는 별도 수치만 보지 않고, handler dispatch, JSON binding, logging,
TLS 여부를 분리해서 측정한다.

내장 HTTP server의 목표는 “동작하는 server”가 아니라 고성능 backend API server다. 기능
기준선으로 `Drogon`, `Oat++`를 참고하듯, 성능 기준선도 같은 계열의 C++ backend framework와
비교한다. 특정 외부 framework에 종속하지는 않지만, 같은 하드웨어와 같은 payload 조건에서
zlink 내장 server가 현저히 느리면 목표를 달성한 것으로 보지 않는다.

기본 benchmark 축:

- plain HTTP route, empty body
- plain HTTP JSON request/reply
- HTTPS JSON request/reply
- keep-alive single connection
- concurrent connections
- handler에서 zlink channel request 호출

HTTP server perf gate는 HTTP handler e2e와 다른 목적을 가진다. HTTP handler e2e는 public
consumer 검증이므로 `zlink::http_client`를 사용한다. 반면 HTTP server perf gate는 client
구현 병목이 server 수치에 섞이지 않도록 같은 load generator로 zlink, Drogon, Oat++ server를
모두 호출한다.

load generator는 runner가 하나로 고정한다. 예를 들어 `wrk`, `wrk2`, `oha` 중 하나를 선택할 수
있지만, 같은 보고서 안에서 zlink, Drogon, Oat++에 서로 다른 도구를 섞으면 안 된다. runner는
도구 이름, version, command line, thread 수, connection 수, duration, warmup을 report에 남긴다.

Perf gate scenario:

| scenario | payload | connection profile | 의미 |
|----------|---------|--------------------|------|
| `http_server_empty_route` | 0B | serial, pipelined, concurrent | route dispatch와 response write 비용 |
| `http_server_json_4kb` | 4KB JSON | serial, pipelined, concurrent | 대표 JSON API 처리량 |
| `https_server_json_4kb` | 4KB JSON | serial, pipelined, concurrent | TLS 포함 JSON API 처리량 |
| `http_server_keep_alive` | 1KB JSON | single connection, pipelined | keep-alive request loop 비용 |
| `http_handler_roundtrip` | 4KB JSON | common framework profile | zlink handler와 messaging까지 포함한 e2e 비용 |

`http_server_empty_route`, `http_server_json_4kb`, `https_server_json_4kb`,
`http_server_keep_alive`는 external baseline과 비교하는 HTTP server gate다.
`http_handler_roundtrip`은 공통 framework perf 정책의 scenario이며, `zlink::http_client`를
사용해 public HTTP client와 handler 통합을 함께 검증한다.

측정 지표:

- requests per second
- p50/p95/p99 latency
- active connection 수
- CPU 사용률
- memory usage
- allocation count
- timeout/reject count

성능 합격 기준:

- 같은 machine, 같은 compiler mode, 같은 payload, 같은 connection 조건에서 `Drogon`과
  `Oat++`로 만든 동등한 route baseline을 함께 측정한다.
- 두 baseline을 모두 만들 수 있으면 더 빠른 baseline을 기준으로 삼는다. 한쪽을 만들 수 없으면
  실패 이유를 기록하고 남은 baseline으로만 임시 판단하되, 최종 완료 gate에는 두 baseline을 모두
  복구해야 한다.
- plain HTTP empty route와 JSON request/reply route는 기준 baseline 대비 처리량 하락이 10%를
  넘으면 완료로 보지 않는다.
- HTTPS JSON route는 TLS 설정 차이가 크므로 별도 baseline을 두고, 같은 TLS 조건에서 처리량
  하락이 15%를 넘으면 완료로 보지 않는다.
- p95 latency가 baseline 대비 15%를 초과해 악화되면 실패로 본다. p99 latency는 노이즈가 크므로
  반복 측정의 중앙값으로 판단한다.
- zlink channel request를 포함한 route는 HTTP server만의 baseline과 분리하고, zlink request
  비용을 포함한 end-to-end 성능 기준으로 관리한다.
- benchmark 결과는 warmup, 반복 횟수, payload size, connection 수, thread 수, TLS 여부,
  baseline framework 이름과 version/commit을 함께 기록해야 한다. 조건이 빠진 수치는 완료 증거로
  쓰지 않는다.
- load generator 이름, version, command line이 빠진 결과는 완료 증거로 쓰지 않는다.
- baseline route는 zlink route와 같은 status code, content type, response body size를 반환해야
  한다. 기능이 다른 route끼리 비교한 결과는 perf gate 증거가 아니다.

CTest의 `framework-http-perf` label은 두 단계로 나눈다. `test_cpp_framework_http_perf_policy`는
문서에 필요한 기준이 빠지지 않았는지 확인한다. `test_cpp_framework_http_perf_gate`는 perf 환경에서
생성한 report를 읽어 baseline 대비 수치를 검증한다. 로컬 개발 환경처럼 Drogon/Oat++ baseline
report가 없을 때는 gate가 수치를 평가하지 않았다고 출력한다. 최종 완료 판단에서는 반드시
`ZLINK_FRAMEWORK_HTTP_PERF_REPORT`를 지정해 report gate를 실행해야 한다.

perf report는 CMake script 형식으로 둔다. runner는 아래 변수를 모두 기록해야 한다.

```cmake
set(ZLINK_HTTP_PERF_LOAD_GENERATOR "oha")
set(ZLINK_HTTP_PERF_LOAD_GENERATOR_VERSION "...")
set(ZLINK_HTTP_PERF_COMMAND_LINE "...")
set(ZLINK_HTTP_PERF_COMPILER "...")
set(ZLINK_HTTP_PERF_BUILD_TYPE "Release")
set(ZLINK_HTTP_PERF_MACHINE "...")
set(ZLINK_HTTP_PERF_SCENARIOS http_server_empty_route http_server_json_4kb)

set(ZLINK_HTTP_PERF_http_server_empty_route_ZLINK_RPS 900000)
set(ZLINK_HTTP_PERF_http_server_empty_route_BASELINE_RPS 1000000)
set(ZLINK_HTTP_PERF_http_server_empty_route_ZLINK_P95_US 900)
set(ZLINK_HTTP_PERF_http_server_empty_route_BASELINE_P95_US 800)
set(ZLINK_HTTP_PERF_http_server_empty_route_BASELINE_NAME "Drogon")
set(ZLINK_HTTP_PERF_http_server_empty_route_MAX_RPS_DROP_PERCENT 10)
set(ZLINK_HTTP_PERF_http_server_empty_route_MAX_P95_SLOWDOWN_PERCENT 15)
```

`ZLINK_FRAMEWORK_HTTP_PERF_REQUIRED=1`을 함께 지정하면 report가 없을 때 바로 실패한다. 이 옵션은
CI perf lane이나 release gate에서 사용한다.

성능 최적화는 public API를 복잡하게 만들지 않는다. queue, parser buffer, connection registry,
TLS context, executor tuning은 runtime option이나 내부 구현으로 숨긴다.

## 18. 테스트 계획

필수 회귀 테스트:

| 테스트 | 기대 |
|--------|------|
| startup validation | invalid endpoint, missing TLS file, duplicate system route 실패 |
| route mapping | `map_get/post/put/delete`가 올바른 handler를 호출 |
| not found | 없는 path는 `404` |
| method not allowed | path는 있으나 method가 다르면 `405` |
| unsupported media type | JSON route에 잘못된 content type이면 `400` |
| malformed body | JSON decode 실패는 `400` |
| body limit | limit 초과는 `413` |
| typed handler shape | DTO, DTO+context, DTO+request, response 반환 sync/async shape 모두 호출 |
| raw request handler | `http_request_t`를 받고 `http_response_t`로 응답 |
| raw handler no serializer | raw route는 request/reply JSON serializer 없이 등록 |
| ambiguous handler shape | 모호한 handler signature는 static assertion 또는 startup validation 실패 |
| keep-alive | 같은 connection에서 두 request 처리 |
| request timeout | header/body timeout이 connection을 닫고 event 기록 |
| handler timeout | `504` response |
| middleware | success, short-circuit, exception에서 after 실행 |
| TLS | HTTPS route 성공, TLS 설정 오류 실패 |
| graceful shutdown | 새 accept 중단, active request drain |
| logging | route, status, duration, correlation id 기록 |
| metrics | request/status/connection counter 갱신 |
| zlink integration | HTTP handler에서 channel request 또는 SPOT call 사용 |

테스트는 public API를 기준으로 작성한다. private socket이나 Beast request 객체를 직접 조작하는
테스트는 runtime unit test로만 제한한다.

## 19. 구현 적합성 요구

- listener, connection, request pipeline과 error mapper 책임을 분리한다.
- endpoint, TLS, body, header와 timeout 설정은 listener 시작 전에 검증한다.
- TLS context는 listener 단위로 재사용한다.
- I/O executor와 handler executor를 분리하고 connection state를 connection별로 직렬화한다.
- route table compile과 buffer reuse를 적용하며 metrics/logging hot path에 global mutex를
  두지 않는다.
- logging, monitoring과 metrics event를 연결한다.
- HTTP server benchmark는 p95/p99 latency, concurrency와 동일 조건의 외부 baseline을
  기록하고 `framework-http-perf` gate로 검증한다.

## 20. 적합성 기준

내장 HTTP server가 backend API framework의 기본 server로 자리 잡으려면 아래 조건을 모두 만족해야
한다.

- `options.http().listen(...).map_*<THandler>(...)` public 표면을 유지한다.
- HTTP/1.1, HTTPS, keep-alive, timeout, limit, graceful shutdown을 지원한다.
- handler는 Beast/Asio/TLS 타입을 알 필요가 없다.
- typed DTO, typed response, raw HTTP request handler shape를 모두 지원한다.
- raw HTTP request handler도 `http_request_t`와 `http_response_t`만 사용하고 Beast/Asio/TLS
  타입을 받지 않는다.
- logging, monitoring, health, DI, serializer와 같은 app model에 통합된다.
- malformed request, route 없음, method mismatch, body limit, handler failure가 status와 JSON body로
  일관되게 매핑된다.
- 샘플과 HTTP e2e는 `zlink::http_client`로 검증한다.
- public header dependency gate에서 Boost.Beast, Boost.Asio, OpenSSL 타입이 노출되지 않는다.
- framework regression label에 HTTP server 테스트가 포함된다.
- connection I/O, route dispatch, handler executor가 분리되어 있고, connection마다 OS thread를
  만들지 않는다.
- route table, TLS context, serializer registry는 startup에서 준비되며 request마다 다시 만들지
  않는다.
- hot path logging이 global mutex나 high-cardinality label에 의존하지 않는다.
- Drogon/Oat++ baseline과 비교한 `framework-http-perf` gate를 통과한다.
- POSD 리뷰에서 남은 얕은 모듈, 정보 누출, 순서 의존 설정 문제가 없어야 한다.

## 21. 참고 기준

이 문서는 특정 외부 framework에 의존하지 않는다. 아래 자료는 기능과 성능 설계 기준을 잡기 위한
참고 기준이다.

- Drogon 공식 문서: <https://drogonframework.github.io/drogon-docs/>
- Drogon repository: <https://github.com/drogonframework/drogon>
- Oat++ 공식 문서: <https://oatpp.io/docs/start/>
- Oat++ repository: <https://github.com/oatpp/oatpp>

---
<!-- framework-adapter-nav:bottom:start -->
[스펙 목차](README.ko.md) | [이전: C++ HTTP Hosting](60-http-hosting.ko.md)
<!-- framework-adapter-nav:bottom:end -->
