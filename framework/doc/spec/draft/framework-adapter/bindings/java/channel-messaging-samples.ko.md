[스펙 목차](../../../README.ko.md)

[Java 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel](./spring-boot-channel-messaging.ko.md)

# Draft -- ZLink Framework Java Channel Messaging Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Java` channel messaging 초안을 코드 흐름으로 한 번에
> 보기 위한 샘플 문서다.

## 1. 자동 연결 샘플

```java
@Configuration
@EnableZLinkFramework
public class ZLinkConfig implements ZLinkFrameworkOptionsCustomizer {
    @Override
    public void customize(ZLinkFrameworkOptions options) {
        options.addChannel("api", channel -> {
            channel.enableServer();
        });

        options.addChannel("profile", channel -> {
            channel.enableClient();
        });

        options.addChannel("account", channel -> {
            channel.enableClient();
        });

        options.useDiscovery(registry -> {
            registry.add("tcp://registry1:5551");
            registry.add("tcp://registry2:5551");
        });
    }
}
```

## 2. 수동 연결 샘플

```java
options.addChannel("profile", channel -> {
    channel.enableClient(client -> {
        client.useManualConnections(peers -> {
            peers.connect("tcp://10.0.10.15:7101");
            peers.connect("tcp://10.0.10.16:7101");
        });
    });
});
```

이 설정은 `profile` channel 전체가 아니라 `profile.client` 연결 집합에만 적용된다.

## 2.1 런타임 수동 연결 제어 샘플

```java
@Component
public final class WarmupTask {
    private final ZLinkChannelConnectionManager connections;

    public WarmupTask(ZLinkChannelConnectionManager connections) {
        this.connections = connections;
    }

    public void warmup() {
        connections.getClient("profile")
            .connect("tcp://10.0.10.17:7101");
    }
}
```

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
        return client.requestAsync(
            "profile",
            new GetProfileRequest(request.accountId()),
            null
        );
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

    @ZLinkRequestMapping
    public CompletionStage<GetUserReply> getUserAsync(
        GetUserRequest request,
        ZLinkRequestContext context) {
        return client.requestAsync(
            "account",
            new GetAccountRequest(request.accountId()),
            null
        ).thenApply(account -> new GetUserReply(
            request.accountId(),
            account.nickname()
        ));
    }
}
```

## 5. Options 예시

```java
var reply = client.requestAsync(
    "profile",
    new GetProfileRequest(accountId),
    new ZLinkRequestOptions()
        .setTimeout(Duration.ofMillis(200))
        .setPacketName("profile.get")
);
```

기본은 payload 타입 이름이고, `packetName`은 정말 필요할 때만 override한다.

## 6. 일반 event publish

```java
eventPublisher.publishAsync(
    "profile",
    "profile.cache-refreshed",
    new ProfileCacheRefreshed(accountId),
    new ZLinkSendOptions().setPacketName("profile.cache-refreshed")
).toCompletableFuture().join();
```
