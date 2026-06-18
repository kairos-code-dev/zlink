# ZLink HTTP Client For Kotlin — 사용자 가이드

`zlink-http-client-kotlin`은 Java `zlink-http-client` **런타임을 그대로 재사용**하는 얇은
idiom 레이어다. 별도 전송 구현은 없고, `CompletionStage`를 진짜 `suspend`로 잇는 coroutine
확장과 DSL만 얹는다. 따라서 client 설정·request·response·인증·redirect·압축 같은 **기능과
의미론은 Java 문서를 그대로 따르고**, 이 가이드는 Kotlin coroutine 표면만 다룬다.

```kotlin
import systems.zlink.httpclient.kotlin.*

suspend fun loadProfile(client: ZLinkHttpClient): PlayerProfile {
    val response = client.get("/players/7281").await<PlayerProfile>()   // suspend, non-blocking
    return response.body()
}
```

## 공유 문서 — 기능과 의미론

아래 13장과 spec은 Kotlin에도 그대로 적용된다. Kotlin은 같은 builder 위에 coroutine
확장만 더한다.

| 장 | 문서 | 내용 |
|----|------|------|
| 1 | [개요](../java/01-overview.ko.md) | 설계 철학, 산출물 경계, 실행 모델 |
| 2 | [시작하기](../java/02-getting-started.ko.md) | 의존성, 첫 요청, 한 줄 요청 |
| 3 | [Client 구성](../java/03-client-configuration.ko.md) | builder 옵션, client 재사용 |
| 4 | [Request 만들기](../java/04-making-requests.ko.md) | HTTP 메서드, query, 헤더, request timeout |
| 5 | [Request Body](../java/05-request-body.ko.md) | JSON, raw, form, multipart, streaming 업로드 |
| 6 | [Response 다루기](../java/06-handling-responses.ko.md) | 응답 구조, `submit`/`fetch`, status 처리 |
| 7 | [비동기와 코루틴](../java/07-async-coroutines.ko.md) | `CompletionStage`, non-blocking, blocking 규칙 |
| 8 | [Streaming](../java/08-streaming.ko.md) | 다운로드 sink, chunked 업로드 |
| 9 | [인증과 TLS](../java/09-authentication-tls.ko.md) | Basic/Bearer, HTTPS 검증, mTLS |
| 10 | [Redirect · Retry · Cookie](../java/10-redirects-retries-cookies.ko.md) | redirect 의미론, 재시도, cookie jar |
| 11 | [Proxy](../java/11-proxy.ko.md) | HTTP proxy, proxy 인증 |
| 12 | [압축](../java/12-compression.ko.md) | gzip/deflate 투명 해제 |
| 13 | [에러 처리](../java/13-error-handling.ko.md) | 예외 모델, retriable, 예외 경로 |

정식 계약과 회귀 테스트 축은 spec 문서
[java-http-client.ko.md](../java/spec/java-http-client.ko.md)가 정본이다.

## 의존성

```kotlin
dependencies {
    implementation(project(":zlink-http-client-kotlin"))
}
```

`zlink-http-client`(java)와 `kotlinx-coroutines-jdk8`을 전이 의존으로 가져온다. Kotlin
`data class` DTO를 위해 `jackson-module-kotlin`도 포함된다.

## DSL 빌더

```kotlin
import systems.zlink.httpclient.kotlin.zlinkHttpClient

val client = zlinkHttpClient("https://game-api.example.internal") {
    json()
    timeout(Duration.ofSeconds(5))
}
```

`zlinkHttpClient(baseUrl) { ... }`는 fluent builder에 `configure` 블록을 적용해 client를
만든다.

## suspend 확장 — 진짜 non-blocking 코루틴

Java 메서드는 `CompletionStage`를 돌려준다. Kotlin 확장은 `kotlinx-coroutines-jdk8`의
`await()`로 이를 잇는 `suspend` 함수다. **어떤 스레드도 park되지 않는다** — 이것이 "코루틴
에서 동작하는 HTTP client"의 정본 경로다.

| 확장 | 반환 | 설명 |
|------|------|------|
| `awaitRaw()` | `RawHttpResponse` | raw 응답까지 suspend |
| `await(type)` | `HttpResponse<T>` | typed JSON 응답까지 suspend |
| `await<T>()` (reified) | `HttpResponse<T>` | `await(T::class.java)` 편의 |
| `awaitDownload { chunk -> }` | `RawHttpResponse` | streaming 다운로드 suspend |

streaming 업로드는 Java builder의 `bodyStream { ... }`을 그대로 쓴다(`Supplier<ByteArray>`).

```kotlin
val chunks = ArrayDeque(listOf("a".toByteArray(), "b".toByteArray()))
client.post("/upload")
    .bodyStream({ if (chunks.isEmpty()) null else chunks.poll() }, "application/octet-stream")
    .awaitRaw()
```

## 코루틴 안에서의 동시성

여러 요청을 `async`로 띄우고 `awaitAll`하면 단일 디스패처에서도 직렬화되지 않는다(네이티브
비동기 I/O).

```kotlin
val results = (1..20).map { async { client.get("/r").awaitRaw() } }.awaitAll()
```

## resume dispatcher

`await()`는 호출한 coroutine의 `CoroutineDispatcher`에서 재개된다. 재개 위치를 바꾸려면
`withContext(dispatcher) { ... }`로 감싼다.
