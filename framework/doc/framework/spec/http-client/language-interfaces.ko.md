# 언어별 인터페이스 정의

> [공통 계약 목차](README.ko.md)
>
> 공통 계약([2](02-client-builder.ko.md)~[9장](09-error-model.ko.md))의 각 개념이
> 언어별로 어떤 **정확한 이름/시그니처**로 노출되는지 정의한다.
> 언어별 공개 표면에 새 심볼을 추가하려면 이 문서와 공통 계약 양쪽에 먼저
> 등재되어야 한다.

## 1. 이름 대응표 (공통 개념 → 언어)

### 1.1 진입점과 client

| 개념 | cpp | dotnet | java | kotlin | node |
| --- | --- | --- | --- | --- | --- |
| client 타입 | `client_t` | `ZLinkHttpClient` | `ZLinkHttpClient` | (java 재사용) | `ZLinkHttpClient` |
| 생성 | `client_t::create(url)` | `ZLinkHttpClient.Create(url)` | `ZLinkHttpClient.create(url)` | `zlinkHttpClient(url) { }` | `ZLinkHttpClient.create(url)` |
| builder 타입 | `client_builder_t` | `ZLinkHttpClientBuilder` | `ZLinkHttpClientBuilder` | (DSL 리시버 = java builder) | `ZLinkHttpClientBuilder` |
| 완성 | `.build()` | `.Build()` | `.build()` | (블록 종료) | `.build()` |
| 종료 | 소멸자 | `Dispose()` | `close()` (AutoCloseable) | `use { }` | `close()` |

### 1.2 builder 옵션 (공통 개념명 → 언어 표기)

케이싱 규칙: cpp `snake_case`, dotnet `PascalCase`, java/kotlin/node `camelCase`.
아래는 규칙에서 벗어나거나 인자형이 다른 것만 명시한다. 나머지 옵션
(`defaultHeader`, `basicAuth`, `bearerToken`, `maxResponseBodySize`,
`trustCertificateFile`, `clientCertificateFile`, `followRedirects`, `retry`,
`cookies`, `proxy`, `proxyBasicAuth`, `compression`)은 케이싱 변환만 다르다.

| 개념 | cpp | dotnet | java/kotlin | node |
| --- | --- | --- | --- | --- |
| `timeout` 인자 | `std::chrono::milliseconds` | `TimeSpan` | `java.time.Duration` | 정수 ms |
| 실행 모델 스위치 | `coroutines()` 3오버로드 | — | — | — |
| codec 등록 | — | `Codecs(Action<IZLinkCodecRegistryBuilder>)` | — | — |

### 1.3 verb와 request builder

| 개념 | cpp | dotnet | java/kotlin | node |
| --- | --- | --- | --- | --- |
| verb 7종 | `get/post/put/delete_/patch/head/options` | `Get/Post/Put/Delete/Patch/Head/Options` | `get/.../delete/...` | `get/.../delete/...` |
| request builder | `request_builder_t` | `ZLinkHttpRequestBuilder` | `ZLinkHttpRequestBuilder` | `ZLinkHttpRequestBuilder` |
| typed body | `body(const T&)` | `Body<T>(value)` | `body(Object)` | `body<T>(value)` |
| raw body | `body(content, content_type)` | `Body(content, contentType)` | `body(content, contentType)` | `body(content, contentType)` |
| streaming 업로드 | `body_stream(provider, ct)` — `std::function<std::optional<std::string>()>` | `BodyStream(Func<byte[]?>, ct)` | `bodyStream(Supplier<byte[]>, ct)` / kotlin `() -> ByteArray?` | `bodyStream(provider, ct)` — `() => Uint8Array \| null` |
| form / multipart | `form` / `multipart` / `multipart_file` | `Form` / `Multipart` / `MultipartFile` | `form` / `multipart` / `multipartFile` | `form` / `multipart` / `multipartFile` |

### 1.4 terminator (목표 계약)

**framework의 세 terminator(`submit`/`async`/`yield`)를 그대로 갖고, awaitable을 쓰지 않는
호출자를 위한 callback 완료 경로를 함께 제공한다**([12 HTTP client](12-http-client.ko.md)).
아래는 **목표 계약**이며 현재 구현과의 차이는
[구현 차이 §12.22](../90-implementation-gap.ko.md)가 소유한다.

| 개념 | cpp | dotnet | java | kotlin | node |
| --- | --- | --- | --- | --- | --- |
| **async** (raw) | `async_raw()` → `task_t<raw_http_response_t>` | `AsyncRaw(ct?)` → `ValueTask<RawHttpResponse>` | `asyncRaw()` → `CompletionStage<RawHttpResponse>` | `awaitRaw()` (suspend) | `asyncRaw()` → `Promise<RawHttpResponse>` |
| **async** (typed) | `async<T>()` → `task_t<http_response_t<T>>` | `Async<T>(ct?)` | `async(Class<T>)` | `await(type)` / `await<T>()` (reified) | `async<T>()` |
| **async** (download) | `download(sink)` | `DownloadAsync(sink, ct?)` | `download(Consumer<byte[]>)` | `awaitDownload(sink)` | `download(sink)` |
| **yield** | `yield<T>()` | `Yield<T>(ct?)` | `yield(Class<T>)` | `yieldAwait<T>()` | `yield<T>()` |
| **submit** (one-way) | `submit()` | `Submit(ct?)` | `submit()` | `submit()` | `submit()` |
| **callback** | `async<T>(callback)` | `Async<T>(callback)` | `async(Class<T>, callback)` | (suspend로 대체) | `async<T>(callback)` |
| blocking 언래핑 | **두지 않는다** | **두지 않는다** | **두지 않는다** | **두지 않는다** | **두지 않는다** |

- `yield`는 **execution scheduler가 주입된 client에서만** 노출된다(§5.3). 단독 사용에서는 없다.
- `.NET`은 `SubmitAsync`처럼 submit 동사를 반복하지 않는다 — `Submit`은 one-way 전용이다.
- kotlin의 `fetch<T>()`는 suspend 함수이며 blocking이 아니다. body만 돌려주는 편의 확장이다.

### 1.5 응답/보조 타입

| 개념 | cpp | dotnet | java/kotlin | node |
| --- | --- | --- | --- | --- |
| raw 응답 | `raw_http_response_t{status, headers, body}` | `RawHttpResponse{Status, Headers, Body}` | `RawHttpResponse(status, headers, body)` record | `RawHttpResponse{status, headers, body}` |
| typed 응답 | `http_response_t<T>{status, headers, body, raw_body}` | `HttpResponse<T>{Status, Headers, Body, RawBody}` | `HttpResponse<T>(...)` record — 메서드 접근 | `HttpResponse<T>{...}` |
| 메서드 enum | `http_method_t` | `ZLinkHttpMethod` | `ZLinkHttpMethod` | `ZLinkHttpMethod` (union) |
| 결과 전달 | `result_t<...>` 봉투 + 예외 | 예외 | 예외 | 예외 |

### 1.6 에러 표면 ([9장](09-error-model.ko.md) 매핑 요약)

| | 예외/실패 타입 | kind 접근 |
| --- | --- | --- |
| cpp | `framework_exception_t` / `result_t` | `framework_error_kind_t` (snake_case, framework 공용 22종) + boundary 상태(`timed_out` 등)는 `code()` |
| dotnet | `ZLinkFrameworkException` | `ZLinkFrameworkErrorKind` (PascalCase) + `IsRetriable` |
| java/kotlin | `ZLinkFrameworkException` | `kind()` (UPPER_SNAKE) + `retriable()` |
| node | `ZLinkFrameworkException` | `ZLinkFrameworkErrorKind` (camelCase) + `isRetriable` |

## 2. 언어별 공개 표면 요약 (비규범)

아래 목록은 언어 간 이름 대응을 읽기 위한 요약이다. 언어별 public 심볼의 정확한
전량과 시그니처는 `languages/<lang>/`의 정식 interface 문서가 소유한다. 이 요약에
보조 타입이 빠져 있다는 이유만으로 구현을 제거하거나 public 계약을 바꾸지 않는다.

- **cpp** `zlink::http_client`: `client_t`, `client_builder_t`,
  `request_builder_t`, `http_method_t`, `http_response_t<T>`,
  `raw_http_response_t`, `coroutine_execute_scheduler_t`,
  `coroutine_resume_scheduler_t`, `framework_resume_scheduler_t`.
  (`body_stream_provider_t`는 `request_builder_t` 안의 중첩 typedef이며 최상위 심볼이 아니다)
- **dotnet** `Zlink.HttpClient`: `ZLinkHttpClient`, `ZLinkHttpClientBuilder`,
  `ZLinkHttpRequestBuilder`, `ZLinkHttpMethod`, `RawHttpResponse`,
  `HttpResponse<T>`.
- **java** `systems.zlink.httpclient`: `ZLinkHttpClient`,
  `ZLinkHttpClientBuilder`, `ZLinkHttpRequestBuilder`, `ZLinkHttpMethod`,
  `RawHttpResponse`, `HttpResponse<T>`.
  (`ZLinkHttpTargetBuilder`, `ZLinkHttpRequestBodyEncoder`는 package-private
  내부 — 공개 아님)
- **kotlin** `systems.zlink.httpclient.kotlin`: `zlinkHttpClient`,
  `awaitRaw`, `await`(2형), `awaitDownload`, `fetch` 확장 함수.
- **node** `@zlink-systems/http-client`: `ZLinkHttpClient`,
  `ZLinkHttpClientBuilder`, `ZLinkHttpRequestBuilder`, `ZLinkHttpMethod`,
  `RawHttpResponse`, `HttpResponse<T>`, `BodyChunkProvider`, `DownloadSink`.
