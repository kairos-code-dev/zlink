<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework Java Interface Catalog](handler-interfaces.ko.md) | [다음: ZLink Framework Spring Boot SPOT](spring-boot-spot.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[Java 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel 샘플](./channel-messaging-samples.ko.md) | [SPOT](./spring-boot-spot.ko.md) | [Registry](./spring-boot-registry.ko.md)

# Draft -- ZLink Framework Spring Boot Channel Messaging

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Spring Boot`에서 channel messaging을 어떤 표면으로
> 드러낼지 정리한다.

## 1. 목표

`Spring Boot` 애플리케이션 안에서 아래 경험을 제공하는 것이 목표다.

- channel 이름 기준 direct call
- bean으로 주입되는 공용 outbound client
- bean으로 주입되는 일반 event publisher
- annotation 기반 request/send handler
- HTTP controller 안에서도 같은 `ZLinkClient` 사용

## 2. 등록 방식

같은 capability는 자동 연결과 수동 연결 중 하나만 선택한다.

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

수동 연결은 아래처럼 둔다.

```java
options.addChannel("profile", channel -> {
    channel.enableClient(client -> {
        client.useManualConnections(peers -> {
            peers.connect("tcp://10.0.10.15:7101");
        });
    });
});
```

앱 전체에서는 capability별로 방식을 나눠 쓸 수 있다.
예를 들어 `profile.client`는 discovery, `account.client`는 manual로 둘 수 있다.

중요한 점은 수동 연결이 `channel` 전체 설정이 아니라 `channel + capability`
설정이라는 점이다. 예를 들어 같은 `profile` channel이라도 `profile.client`와
`profile.subscriber`는 다른 연결 집합이다.

여기서 client manual 연결은 remote `RoutingId`를 따로 받지 않는다. channel client는
하부 `DEALER(client)`가 connect된 peer 집합으로 요청을 보내는 모델이므로,
startup과 런타임 제어 모두 endpoint 집합만 관리하면 된다.

manual capability는 startup 등록만이 아니라 런타임 `connect`, `disconnect`,
`listConnections` 제어도 지원해야 한다.

일반 `PUB/SUB` event publish는 `ZLinkEventPublisher` 같은 별도 surface로 설명하는
편이 맞다. 이 표면도 `channel name + topic` 기준으로 동작한다.

## 3. Handler 모델

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

기본 packet key는 `GetUserRequest` 같은 payload 타입 이름을 쓴다.
외부 계약 때문에 다른 이름이 필요할 때만 annotation 또는 options에서 override한다.

## 4. Dispatch 기준

- 일반 request/send handler dispatch는 local `ROUTER(server)`가 받은 메시지 기준이다.
- outbound `DEALER(client)`가 받은 메시지는 reply correlation 경로로 본다.
- 따라서 `ROUTER -> DEALER` 임의 push는 현재 channel messaging 공용 계약에 넣지 않는다.

## 5. Outbound-only 앱

local handler 없이 client만 쓰는 앱도 가능해야 한다.

```java
@Configuration
@EnableZLinkFramework
public class OutboundOnlyConfig implements ZLinkFrameworkOptionsCustomizer {
    @Override
    public void customize(ZLinkFrameworkOptions options) {
        options.addChannel("profile", channel -> {
            channel.enableClient();
        });
        options.useDiscovery(registry -> registry.add("tcp://registry1:5551"));
    }
}
```

이 경우 local `ROUTER(server)`는 열지 않고 outbound `DEALER(client)`만 만든다.
