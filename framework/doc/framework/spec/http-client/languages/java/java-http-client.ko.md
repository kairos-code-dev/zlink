# Spec -- ZLink HTTP Client For Java

> 사용법 중심 문서는 [사용자 가이드](../../../../../http-client/java/README.ko.md)를 본다.
> **언어 중립 공통 계약은 [공통 spec](../../README.ko.md)이 정본**이며,
> 이 문서는 공통 계약에 대한 Java 고유 편차와 구현 매핑만 기술한다.
> 실제 계약의 단일 기준은 공통 spec + `src/main/java/systems/zlink/httpclient/**`
> 공개 타입과 `src/test/java/...` 회귀 테스트다.

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
- `ZLinkHttpClientBuilder` — `baseUrl`, `timeout`, `defaultHeader`, `basicAuth`,
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

기본값·redirect·retry·cookie·압축·인증 스크럽·body 소스 배타 의미론은
[공통 spec 2~8장](../../README.ko.md)을 따른다. Java 구현 매핑:

- redirect: `java.net.http`의 `Redirect` enum에 횟수 한도가 없어 `NEVER`로 두고
  래퍼 루프로 구현.
- cookie: JDK `CookieManager` 미사용, 래퍼 jar로 구현.
- 압축 해제: `java.util.zip`(`java.net.http`는 auto-decompress 안 함).
- TLS: `trustCertificateFile`→TrustManager, mTLS→KeyManager(`SSLContext`).
- proxy: `ProxySelector` + `Proxy-Authorization` 헤더.

## 6. 에러 매핑

[공통 spec 9장](../../09-error-model.ko.md)을 따른다. 모든 실패는
`ZLinkFrameworkException`이며 `kind()`(`REQUEST_PROTOCOL_ERROR`/`REQUEST_FAILED`/
`PAYLOAD_DECODE_FAILED`)와 `retriable()`을 노출한다.

- timeout은 `REQUEST_FAILED`(`retriable=true`) + `HttpTimeoutException` cause.
- 내부 retry 판단은 `IOException`/`UncheckedIOException`/`TimeoutException` 여부.

## 7. 회귀 테스트 / 등록

- 회귀 테스트: `src/test/java`(JUnit 5). chunked 업로드·retry는 raw `ServerSocket`, TLS/mTLS는
  `com.sun.net.httpserver.HttpsServer` + `src/test/resources/tls/` 인증서로 검증.
- 등록: `settings.gradle.kts` `include`에 `zlink-http-client` 추가.
- 커버리지: JaCoCo(`jacocoTestCoverageVerification`) LINE 80% 초과 게이트를 `check`에 연결.
