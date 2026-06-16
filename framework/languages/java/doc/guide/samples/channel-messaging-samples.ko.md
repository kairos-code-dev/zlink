<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework For Java](../../README.ko.md) | [다음: ZLink Framework Java Interface Catalog](../../spec/handler-interfaces.ko.md)
<!-- framework-adapter-nav:end -->

[Java 문서](../../README.ko.md)

[Java 묶음](../../README.ko.md) | [인터페이스](../../spec/handler-interfaces.ko.md) | [channel](../../spec/spring-boot-channel-messaging.ko.md)

# ZLink Framework Java Channel Messaging Samples

## 1. 자동 연결 샘플

```java
@Configuration
@EnableZLinkFramework
public class ZLinkConfig implements ZLinkFrameworkConfigurer {
    @Override
    public void configure(ZLinkFrameworkOptions framework) {
        framework.addClientServerChannel("api")
            .enableServer("tcp://0.0.0.0:7100");

        framework.addClientServerChannel("profile")
            .enableClient();

        framework.addClientServerChannel("account")
            .enableClient();

        ZLinkDiscoveryBuilder discovery = framework.useDiscovery();
        discovery.addRegistryEndpoint("tcp://registry1:5551");
        discovery.addRegistryEndpoint("tcp://registry2:5551");
    }
}
```

## 2. 수동 연결 샘플

```java
framework.addClientServerChannel("profile")
    .enableClient("tcp://10.0.10.15:7101")
    .enableClient("tcp://10.0.10.16:7101");
```

이 설정은 `profile` channel 전체가 아니라 `profile.client` 연결 집합에만 적용된다.

## 2.1 수동 연결 설정 기준

수동 연결은 startup builder 에서 역할 단위로 설정한다. public 계약은 host 시작 뒤
endpoint 를 바꾸는 별도 연결 관리 API 를 제공하지 않는다.

## 3. HTTP controller 안에서 호출

```java
@RestController
@RequestMapping("/profiles")
public final class ProfileController {
    private final ZLinkClient client;

    public ProfileController(ZLinkClient client) {
        this.client = client;
    }

    @PostMapping("/get")
    public CompletionStage<GetProfileReply> get(@RequestBody GetProfileHttpRequest request) {
        return client.requestToChannel(
            "profile",
            new GetProfileRequest(request.accountId())
        ).submit(GetProfileReply.class);
    }
}
```

## 4. Handler 안에서 다른 channel 호출

```java
@Component
public final class UserHandlers {
    private final ZLinkClient client;

    public UserHandlers(ZLinkClient client) {
        this.client = client;
    }

    @ZLinkRequest
    public CompletionStage<GetUserReply> getUserAsync(
        GetUserRequest request,
        ZLinkRequestContext context) {
        return client.requestToChannel(
            "account",
            new GetAccountRequest(request.accountId())
        ).submit(GetAccountReply.class).thenApply(account -> new GetUserReply(
            request.accountId(),
            account.nickname()
        ));
    }
}
```

## 5. Options 예시

```java
var reply = client.requestToChannel(
    "profile",
    new GetProfileRequest(accountId)
).timeout(Duration.ofMillis(200))
 .packetName("profile.get")
 .submit(GetProfileReply.class);
```

기본은 payload 타입 이름이고, `packetName`은 정말 필요할 때만 override한다.

## 6. 일반 event publish

```java
CompletionStage<Void> submitted = fanoutClient.publish(
    "profile",
    "profile.cache-refreshed",
    new ProfileCacheRefreshed(accountId)
).packetName("profile.cache-refreshed")
 .submit();
```

## 7. Routed channel 호출

```java
routeClient.request(
    "play-route",
    targetNodeRid,
    new InspectRoomRequest(roomId)
).timeout(Duration.ofSeconds(1))
 .submit(InspectRoomReply.class);
```

routed channel은 target node를 직접 지정하는 application route 용도다. session actor
dispatch는 이 샘플 경로가 아니라 `STREAM`의 ActorGateway attach 경로를 사용한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework For Java](../../README.ko.md) | [다음: ZLink Framework Java Interface Catalog](../../spec/handler-interfaces.ko.md)
<!-- framework-adapter-nav:bottom:end -->
