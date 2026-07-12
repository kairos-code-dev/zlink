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

### 1.4 제출(terminal)

| 개념 | cpp | dotnet | java | kotlin | node |
| --- | --- | --- | --- | --- | --- |
| raw | `submit_raw()` → `task_t<raw_http_response_t>` | `SubmitRawAsync(ct?)` → `ValueTask<RawHttpResponse>` | `submitRaw()` → `CompletionStage<RawHttpResponse>` | `awaitRaw()` (suspend) | `submitRaw()` → `Promise<RawHttpResponse>` |
| typed | `submit<T>()` → `task_t<http_response_t<T>>` (+콜백 오버로드) | `SubmitAsync<T>(ct?)` | `submit(Class<T>)` | `await(type)` / `await<T>()` (reified) | `submit<T>()` |
| 다운로드 | `download(sink)` — `std::function<void(std::string_view)>` | `DownloadAsync(Action<ReadOnlyMemory<byte>>, ct?)` | `download(Consumer<byte[]>)` | `awaitDownload((ByteArray) -> Unit)` | `download(sink)` — `(Uint8Array) => void` |
| blocking 언래핑 | `fetch<T>()` | `Fetch<T>()` | `fetch(Class<T>)` (`.join()`) | `fetch<T>()` — **suspend, non-blocking** (동명이의, [R5](10-revision-candidates.ko.md)) | **없음** (의도) |
| body만 (async) | — | — | — | `fetch<T>()` | `fetch<T>()` → `Promise<T>` |

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
| cpp | `framework_exception_t` / `result_t` | `framework_error_kind_t` (snake_case, 5종) |
| dotnet | `ZLinkFrameworkException` | `ZLinkFrameworkErrorKind` (PascalCase) + `IsRetriable` |
| java/kotlin | `ZLinkFrameworkException` | `kind()` (UPPER_SNAKE) + `retriable()` |
| node | `ZLinkFrameworkException` | `ZLinkFrameworkErrorKind` (camelCase) + `isRetriable` |

## 2. 언어별 공개 표면 전량 (normative)

각 언어의 공개 심볼은 아래 목록이 전부다. 목록 밖 심볼을 공개하면 계약 위반.

- **cpp** `zlink::http_client`: `client_t`, `client_builder_t`,
  `request_builder_t`, `http_method_t`, `http_response_t<T>`,
  `raw_http_response_t`, `body_stream_provider_t`,
  `coroutine_execute_scheduler_t`, `coroutine_resume_scheduler_t`,
  `framework_resume_scheduler_t`.
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
