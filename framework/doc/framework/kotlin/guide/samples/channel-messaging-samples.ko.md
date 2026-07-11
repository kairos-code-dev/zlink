<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework For Kotlin](../../README.ko.md) | [다음: ZLink Framework Java Interface Catalog](../../../common/spec/languages/java/handler-interfaces.ko.md)
<!-- framework-adapter-nav:end -->

[Kotlin 묶음](../../README.ko.md) | [인터페이스](../../../common/spec/languages/java/handler-interfaces.ko.md) | [channel](../../../common/spec/languages/java/spring-boot-channel-messaging.ko.md)

# ZLink Framework Kotlin Channel Messaging Samples

## 1. 자동 연결 샘플

```kotlin
@Configuration
@EnableZLinkFramework
class ZLinkConfig {
    @Bean
    fun framework(): ZLinkFrameworkConfigurer =
        ZLinkFrameworkConfigurer { options ->
            options.useCoroutineHandlers(Dispatchers.Default)
            options.addClientServerChannel("api").enableServer("tcp://0.0.0.0:7100")
            options.addClientServerChannel("profile").enableClient()
            options.addClientServerChannel("account").enableClient()

            val discovery = options.useDiscovery()
            discovery.addRegistryEndpoint("tcp://registry1:5551")
            discovery.addRegistryEndpoint("tcp://registry2:5551")
        }
}
```

## 2. 수동 연결 샘플

```kotlin
options.addClientServerChannel("profile")
    .enableClient("tcp://10.0.10.15:7101")
    .enableClient("tcp://10.0.10.16:7101")
```

이 설정은 `profile` channel 전체가 아니라 `profile.client` 연결 집합에만 적용된다.

## 2.1 수동 연결 설정 기준

수동 연결은 startup builder 에서 역할 단위로 설정한다. public 계약은 host 시작 뒤
endpoint 를 바꾸는 별도 연결 관리 API 를 제공하지 않는다.

## 3. HTTP controller 안에서 호출

controller는 `suspend fun`으로 두고 `request<T>` 확장으로 호출한다.

```kotlin
@RestController
@RequestMapping("/profiles")
class ProfileController(private val client: ZLinkClient) {
    @PostMapping("/get")
    suspend fun get(@RequestBody request: GetProfileHttpRequest): GetProfileReply =
        client.request("profile", GetProfileRequest(request.accountId))
}
```

## 4. Handler 안에서 다른 channel 호출

```kotlin
@ZLinkHandlerGroup("api")
class UserHandlers(private val client: ZLinkClient) {
    @ZLinkRequest
    suspend fun getUser(request: GetUserRequest, context: ZLinkRequestContext): GetUserReply {
        val account: GetAccountReply =
            client.request("account", GetAccountRequest(request.accountId))
        return GetUserReply(request.accountId, account.nickname)
    }
}
```

## 5. Options 예시

```kotlin
@ZLinkPacket("profile.get")
data class GetProfilePacket(val accountId: String)

val reply: GetProfileReply =
    client.requestToChannel("profile", GetProfilePacket(accountId))
        .timeout(Duration.ofMillis(200))
        .submit(GetProfileReply::class.java)
        .await()
```

기본은 payload 타입 이름이다. 타입 이름 대신 외부 계약 이름을 써야 하면 payload 타입에
`@ZLinkPacket(...)`을 붙인다. 같은 payload 타입을 호출마다 다른 packet 이름으로 보내야
하는 예외에서만 `packetName(...)`을 per-call override로 쓴다.

## 6. 일반 event publish

```kotlin
@ZLinkPacket("profile.cache-refreshed")
data class ProfileCacheRefreshedEvent(val accountId: String)

fanoutClient.publishToTopic(
    "profile",
    "profile.cache-refreshed",
    ProfileCacheRefreshedEvent(accountId),
)   // suspend 확장
```

## 7. Routed channel 호출

```kotlin
val reply: InspectRoomReply =
    routeClient.requestTo("play-route", targetNodeRid, InspectRoomRequest(roomId))
        .timeout(Duration.ofSeconds(1))
        .submit(InspectRoomReply::class.java)
        .await()
```

routed channel은 target node를 직접 지정하는 application route 용도다. session actor
dispatch는 이 샘플 경로가 아니라 `STREAM`의 session relay 경로를 사용한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework For Kotlin](../../README.ko.md) | [다음: ZLink Framework Java Interface Catalog](../../../common/spec/languages/java/handler-interfaces.ko.md)
<!-- framework-adapter-nav:bottom:end -->
