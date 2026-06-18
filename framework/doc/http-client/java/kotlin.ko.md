[← 목차](README.ko.md)

# Kotlin coroutine 확장

`zlink-http-client-kotlin`은 Java `zlink-http-client` **런타임을 그대로 재사용**하는 얇은
idiom 레이어다. 별도 전송 구현은 없고, `CompletionStage`를 진짜 `suspend`로 잇는 coroutine
확장과 DSL만 얹는다. 이 문서는 kotlin 특징만 다루며, 기능·의미론은 Java 가이드
([README](README.ko.md))를 따른다.

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

```kotlin
import systems.zlink.httpclient.kotlin.*

suspend fun loadProfile(client: ZLinkHttpClient): PlayerProfile {
    val response = client.get("/players/7281").await<PlayerProfile>()   // suspend, non-blocking
    return response.body()
}
```

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

[← 목차](README.ko.md)
