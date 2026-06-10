<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Stream Connector For C++](./cpp-stream-connector.ko.md) | [다음: Draft -- ZLink Framework C++ HTTP Hosting](./cpp-http-hosting.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md) | [C++ 정책](./cpp-framework-policy.ko.md) | [Application Framework](./cpp-application-framework.ko.md) | [Framework 인터페이스](./cpp-framework-interfaces.ko.md) | [HTTP Hosting](./cpp-http-hosting.ko.md)

# Draft -- ZLink HTTP Client For C++

> 이 문서는 **draft 계약**이다.
> `zlink::http_client` 산출물의 현재 구현 범위와 다음 구현 범위를 함께 정리한다.
> 정식 spec으로 승격하기 전까지는 `core/include/zlink.h` 기준 공개 계약이 아니다.

## 1. 목적

`zlink::http_client`는 C++ 샘플과 테스트에서 HTTP/JSON request를 보내기 위한 별도
client-side 산출물이다. C++ HTTP client 구현은 보통 낮은 수준 타입과 설정이 많으므로,
샘플마다 별도 wrapper를 만들지 않고 zlink의 call object와 fluent builder 스타일로
복잡성을 흡수한다.

이 client는 framework HTTP hosting을 검증하는 소비자다. `zlink::framework` core target의
기본 의존성이 아니며, framework public header가 이 client를 include하지 않는다.

## 2. 산출물 경계

public contract와 runtime 구현은 아래처럼 나눈다.

| 역할 | 위치 | 공개 여부 |
|------|------|-----------|
| facade header | `http-client/include/zlink/http_client.hpp` | public |
| contract header | `http-client/include/zlink/http_client/contracts/*` | public |
| runtime 구현 | `http-client/src/runtime/*` | private |
| CMake target | `zlink::http_client` | public target |

public header에는 `Boost.Beast`, `Boost.Asio`, OpenSSL, socket, resolver, request parser,
response parser, SSL stream, SSL context 타입을 노출하지 않는다.

현재 구현된 public 산출물은 아래와 같다.

- `zlink/http_client.hpp`
- `zlink/http_client/contracts/client.hpp`
- `zlink::http_client` CMake target
- `client_t::create().base_url(...).json().timeout(...).trust_certificate_file(...).build()`
- `get`, `post`, `put`, `delete_` request builder
- typed JSON `body(...)`, `submit<T>()`, `submit_raw()`

## 3. Public API Shape

기본 사용 흐름은 아래 형태다.

```cpp
auto client = zlink::http_client::client_t::create()
  .base_url(topology.api_http_endpoint)
  .json()
  .timeout(std::chrono::seconds(3))
  .build();

auto created = co_await client
  .post("/games")
  .body(create_match_req_t { .owner_actor_id = actor_id })
  .submit<create_match_res_t>();
```

typed submit은 내부에서 raw submit 결과를 `.result()`로 기다리지 않고 `task_t` 완료를
await한다. 이 규칙은 샘플 handler가 HTTP client를 사용할 때 runtime thread를 막지 않도록
하기 위한 것이다.

초기 범위는 아래로 제한한다.

- `GET`, `POST`, `PUT`, `DELETE`
- typed JSON request body
- typed JSON response
- timeout
- default header
- HTTP status mapping
- HTTP와 HTTPS endpoint
- TLS server certificate verification
- hostname verification
- test certificate trust option

`base_url(...)`, `timeout(...)`, `default_header(...)`, `trust_certificate_file(...)`,
request path, request header name은 call을 보내기 전에 검증한다. URL scheme이 `http://` 또는
`https://`가 아니거나 timeout이 0 이하인 경우, 또는 이름이 비어 있는 경우에는
`framework_error_kind_t::request_protocol_error`로 설정 오류를 알린다. 이 오류는 transport
실패와 구분되어야 하므로 `request_failed`로 뭉개지 않는다.

retry, redirect, cookie, proxy, multipart, streaming download는 초기 범위에 넣지 않는다.
필요하면 별도 extension point로 설계한다.

## 4. JSON 계약

JSON 변환은 `message_t` 또는 DTO serializer hook을 통해 처리한다. 샘플 application code가
`nlohmann::json::parse`로 field를 직접 꺼내지 않는다.

`HttpClient.PostAsJsonAsync(...)`와 `ReadFromJsonAsync<T>()`에 대응하는 C++ 흐름은 아래와
같다.

| .NET 흐름 | C++ 흐름 |
|-----------|----------|
| `PostAsJsonAsync(...)` | `client.post(path).body(dto).submit<TReply>()` |
| `ReadFromJsonAsync<T>()` | `message_t::parse_json<T>()` 기반 response decode |

## 5. HTTPS와 TLS

`base_url(...)`은 `http://`와 `https://` endpoint를 모두 받는다.

`https://` request는 아래 항목을 수행한다.

- TLS handshake
- server certificate verification
- hostname verification

test certificate나 local development certificate를 trust하는 설정은 HTTP client option으로
명시해야 한다. 묵시적으로 TLS verification을 끄지 않는다.

## 6. HTTP Hosting 테스트에서의 사용

HTTP handler e2e 테스트는 외부 HTTP 도구나 sample-local client가 아니라
`zlink::http_client`로 `GET`, `POST`, `PUT`, `DELETE` route를 호출한다.

이 규칙은 두 가지를 보장하기 위한 것이다.

- HTTP hosting handler가 실제 public client로 검증된다.
- 샘플과 테스트가 서로 다른 HTTP wrapper를 갖지 않는다.

## 7. 회귀 테스트

최소 테스트는 아래 축으로 둔다.

- contract header compile: `#include <zlink/http_client.hpp>`
- public header boundary: runtime 구현 header와 Beast/Asio/OpenSSL 타입이 public header에
  드러나지 않는다
- JSON request/response: typed DTO request를 JSON으로 보내고 reply DTO를 읽는다
- coroutine submit: `co_await submit<T>()`가 typed response를 반환하고 내부 raw submit을
  blocking wait로 기다리지 않는다
- HTTP status mapping: `400`, `404`, `500` 응답이 client result/error kind로 고정된다
- timeout: 응답 지연은 timeout error로 닫힌다
- fluent input validation: 잘못된 base URL, path, header name, timeout은
  `request_protocol_error`로 닫힌다
- HTTPS success: test certificate trust 설정이 있으면 `https://` JSON request/response가
  성공한다
- TLS failure: 신뢰하지 않은 certificate와 hostname mismatch는 명시적인 client error로
  실패한다

현재 회귀 테스트는 `test_cpp_http_client`와 `test_cpp_framework_contract_headers`가 담당한다.
OpenSSL을 찾은 빌드에서는 configure 단계에서 테스트 인증서를 생성하고
`test_cpp_http_client` 안에서 HTTPS success, untrusted certificate failure, hostname
mismatch failure를 함께 검증한다.

검증 label은 아래와 같다.

```bash
ctest --test-dir framework/languages/cpp/build -L http-client-contract
ctest --test-dir framework/languages/cpp/build -L http-client-unit
ctest --test-dir framework/languages/cpp/build -L http-client-e2e
ctest --test-dir framework/languages/cpp/build -L http-client-https
ctest --test-dir framework/languages/cpp/build -L http-client-regression
```
