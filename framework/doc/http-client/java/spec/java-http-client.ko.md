# Spec -- ZLink HTTP Client For Java

> 사용법 중심 문서는 [사용자 가이드](../README.ko.md)를 본다.
> 이 문서는 `zlink-http-client` 산출물의 공개 계약을 정리한다.
> 실제 계약의 단일 기준은 `src/main/java/systems/zlink/httpclient/**` 공개 타입과
> `src/test/java/...` 회귀 테스트다.

## 1. 목적

`zlink-http-client`는 Java에서 HTTP request를 보내기 위한 별도 client-side 산출물이다.
JSON 전용 client가 아니라 일반 HTTP client이며 zlink fluent builder 스타일로
`java.net.http`의 낮은 수준 설정을 흡수한다. typed JSON 경로(`body(dto)`/`submit(Type)`/
`fetch(Type)`)는 그 위에 얹은 편의 계층이다.

`zlink-framework-core`의 에러 모델(`ZLinkFrameworkException`)에 의존하지만 framework core의
기본 의존성은 아니다(단방향 의존).

## 2. 산출물 경계

| 역할 | 위치 | 공개 여부 |
|------|------|-----------|
| 공개 contract | `systems.zlink.httpclient.{ZLinkHttpClient,ZLinkHttpClientBuilder,ZLinkHttpRequestBuilder,RawHttpResponse,HttpResponse,ZLinkHttpMethod}` | public |
| runtime 구현 | `systems.zlink.httpclient.internal.*` | internal |
| 회귀 테스트 | `src/test/java/...` | private |
| Gradle 서브프로젝트 | `zlink-http-client` | public |

공개 표면에는 `java.net.http`의 `HttpClient`/`HttpRequest`/`HttpResponse` 타입을 노출하지
않는다.

## 3. 공개 타입

- `ZLinkHttpClient` — `create()` / `create(baseUrl)`, 메서드 `get/post/put/delete/
  patch/head/options`, `AutoCloseable`.
- `ZLinkHttpClientBuilder` — `baseUrl`, `json`, `timeout`, `defaultHeader`, `basicAuth`,
  `bearerToken`, `maxResponseBodySize`, `trustCertificateFile`, `clientCertificateFile`,
  `followRedirects`, `retry`, `cookies`, `proxy`, `proxyBasicAuth`, `compression`,
  `build`, 그리고 단발 verb shortcut.
- `ZLinkHttpRequestBuilder` — `header`, `query`, `timeout`, `body(Object)`(JSON),
  `body(String, String)`(raw), `bodyStream(Supplier<byte[]>, String)`, `form`, `multipart`,
  `multipartFile`, `submitRaw`, `download(Consumer<byte[]>)`, `submit(Class<T>)`,
  `fetch(Class<T>)`.
- `RawHttpResponse`(record) { `status`, `headers`, `body` }.
- `HttpResponse<T>`(record) { `status`, `headers`, `body`, `rawBody` }.
- `ZLinkHttpMethod`(enum).

## 4. 실행 모델

- `submitRaw`/`submit`/`download`는 `CompletionStage`를 돌려준다. `java.net.http`의 NIO
  비동기 I/O로 네트워크 대기 중 호출 스레드는 점유되지 않는다. redirect/retry 루프도
  `CompletionStage` 체인으로 합성된다.
- handler 경로는 `CompletionStage` 합성만 쓰고 `.get()`/`.join()`은 금지(blocking).
- `fetch(Type)`는 blocking 접근으로 테스트·CLI 전용.
- continuation 재개 위치는 `CompletableFuture.*Async(fn, executor)` 조합으로 지정.

## 5. 전송 의미론

- **redirect**: `301/302/303/307/308` + `Location`. `303`/(`301`·`302`+`POST`)→`GET`,
  본문 제거. same-origin `Authorization` 보존, cross-origin 제거. `Redirect` enum에 횟수가
  없어 `NEVER`로 두고 래퍼 루프로 구현.
- **retry**: `IOException` 기반 retriable 실패만, 고정 50ms, streaming 제외.
- **cookie jar**: host 정확 매칭, 기본 `Path=/`, `Path`/`Secure`/`Max-Age`만, host당 128개.
  JDK `CookieManager` 미사용.
- **compression**: gzip+deflate 해제, `content-encoding` 제거, decoded 크기 한도,
  streaming 비해제. `java.net.http`는 auto-decompress 안 함.
- **TLS**: `trustCertificateFile`→TrustManager, mTLS→KeyManager(`SSLContext`).
- **proxy**: `ProxySelector` + `Proxy-Authorization` 헤더.
- **body 소스 상호 배타**: `body`/`bodyStream`/`form`/`multipart` 중 하나.

## 6. 에러 매핑

모든 실패는 `ZLinkFrameworkException`으로 보고된다(Java framework는 kind enum/`isRetriable`
을 노출하지 않음). retry 판단은 내부적으로 `IOException` 여부로 한다. timeout은
`HttpTimeoutException`(IOException)이므로 retriable.

## 7. 회귀 테스트 / 등록

- 회귀 테스트: `src/test/java`(JUnit 5). chunked 업로드·retry는 raw `ServerSocket`, TLS/mTLS는
  `com.sun.net.httpserver.HttpsServer` + `src/test/resources/tls/` 인증서로 검증.
- 등록: `settings.gradle.kts` `include`에 `zlink-http-client` 추가.
- 커버리지: JaCoCo(`jacocoTestCoverageVerification`) LINE 80% 초과 게이트를 `check`에 연결.
