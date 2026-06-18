# Spec -- ZLink HTTP Client For Kotlin

> 사용법 중심 문서는 [사용자 가이드](../README.ko.md)를 본다.
> 이 문서는 `zlink-http-client-kotlin` 산출물의 공개 계약을 정리한다.
> 실제 계약의 단일 기준은
> `src/main/kotlin/systems/zlink/httpclient/kotlin/HttpClientCoroutines.kt` 공개 확장과
> `src/test/kotlin/...` 회귀 테스트다.

## 1. 목적

`zlink-http-client-kotlin`은 Kotlin coroutine으로 HTTP request를 보내기 위한 산출물이다.
검증된 `zlink-http-client` 전송 런타임을 전이 의존으로 재사용하고 그 위에 DSL과 진짜
`suspend` 확장만 얹는다. 모든 제출은 non-blocking coroutine이며 호출한 coroutine의
dispatcher에서 재개된다.

## 2. 산출물 경계

| 역할 | 위치 | 공개 여부 |
|------|------|-----------|
| 공개 contract | `systems.zlink.httpclient.kotlin.HttpClientCoroutines.kt`의 top-level 확장 | public |
| 재사용 런타임 | `zlink-http-client`(전이 의존) | public |
| 회귀 테스트 | `src/test/kotlin/...` | private |
| Gradle 서브프로젝트 | `zlink-http-client-kotlin` | public |

## 3. 공개 표면

DSL과 확장은 `systems.zlink.httpclient.kotlin` 패키지의 top-level 함수다.

- `zlinkHttpClient(baseUrl: String, configure: ZLinkHttpClientBuilder.() -> Unit = {}): ZLinkHttpClient`
  — DSL 블록을 fluent builder에 적용해 client를 만든다. 블록 안에서 `json`/`timeout`/
  `basicAuth`/`bearerToken`/`maxResponseBodySize`/`trustCertificateFile`/
  `clientCertificateFile`/`followRedirects`/`retry`/`cookies`/`proxy`/`proxyBasicAuth`/
  `compression` 등 builder 메서드를 그대로 호출한다.
- `suspend ZLinkHttpRequestBuilder.awaitRaw(): RawHttpResponse`
- `suspend ZLinkHttpRequestBuilder.await(type: Class<T>): HttpResponse<T>`
- `suspend inline fun <reified T> ZLinkHttpRequestBuilder.await(): HttpResponse<T>`
- `suspend inline fun <reified T> ZLinkHttpRequestBuilder.fetch(): T` — `await<T>().body()` 편의.
- `suspend ZLinkHttpRequestBuilder.awaitDownload(sink: (ByteArray) -> Unit): RawHttpResponse`

request 구성(`get/post/put/delete/patch/head/options`, `header`, `query`, `timeout`,
`body`, `bodyStream`, `form`, `multipart`, `multipartFile`)과 응답 타입
(`RawHttpResponse`, `HttpResponse<T>`)은 재사용 런타임의 공개 타입을 그대로 쓴다.

## 4. 실행 모델

- 모든 확장(`awaitRaw`/`await`/`fetch`/`awaitDownload`)은 `suspend` 함수다. 내부
  `CompletionStage`를 `kotlinx-coroutines-jdk8`의 `await()`로 잇는다. 네트워크 대기 중
  스레드는 점유되지 않는다.
- handler·actor·spot 경로는 suspend 함수 안에서 직접 호출한다. `runBlocking`은 테스트·CLI
  전용이다.
- continuation은 호출한 coroutine의 dispatcher에서 재개된다. 재개 위치는 `withContext`로
  바꾼다.

## 5. 전송 의미론

- **redirect**: `301/302/303/307/308` + `Location`. `303`/(`301`·`302`+`POST`)→`GET`,
  본문 제거. same-origin `Authorization` 보존, cross-origin 제거. 횟수 한도 초과 시 예외.
- **retry**: retriable transport 실패(`IOException`)만, 고정 50ms 간격, streaming 제외.
  status 코드(4xx/5xx)는 재시도하지 않는다.
- **cookie jar**: host 정확 매칭, 기본 `Path=/`, `Path`/`Secure`/`Max-Age`만 해석,
  secure cookie는 https에만, host당 128개.
- **compression**: gzip+deflate 해제, `content-encoding` 헤더 제거, decoded 크기 한도,
  streaming chunk는 비해제.
- **TLS**: `trustCertificateFile`로 테스트 인증서 신뢰, `clientCertificateFile`로 mTLS.
- **proxy**: `proxy(url)` + `proxyBasicAuth` → `Proxy-Authorization`.
- **body 소스 상호 배타**: `body`/`bodyStream`/`form`/`multipart` 중 하나.

## 6. 에러 매핑

모든 실패는 `ZLinkFrameworkException`(`systems.zlink.framework.errors`)으로 보고된다. suspend
호출은 `try`/`catch`로 잡는다. retry 판단은 내부 `IOException` 여부로 한다.

## 7. 회귀 테스트 / 등록

- 회귀 테스트: `src/test/kotlin`(JUnit 5 + `runBlocking`). `awaitRaw`/typed `await`/
  `awaitDownload`/streaming 업로드/동시성(`async`+`awaitAll`)을 검증한다.
- 등록: `settings.gradle.kts` `include`에 `zlink-http-client-kotlin` 추가.
