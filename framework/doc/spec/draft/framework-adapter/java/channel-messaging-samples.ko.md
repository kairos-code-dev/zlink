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
        options.setChannelName("api");
        options.addOutboundChannel("profile");
        options.addOutboundChannel("account");
        options.useDiscovery(registry -> {
            registry.add("tcp://registry1:5551");
            registry.add("tcp://registry2:5551");
        });
    }
}
```

## 2. 수동 연결 샘플

```java
options.configureManualConnections(connections -> {
    connections.add("profile", peers -> {
        peers.connect(RoutingId.parse("01HZX..."), "tcp://10.0.10.15:7101");
        peers.connect(RoutingId.parse("01HZY..."), "tcp://10.0.10.16:7101");
    });
});
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
