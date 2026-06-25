<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework Java SPOT Samples](../guide/samples/spot-samples.ko.md) | [다음: ZLink Framework Spring Boot Monitoring](spring-boot-monitoring.ko.md)
<!-- framework-adapter-nav:end -->

[Java spec 목차](README.ko.md)

[Java 묶음](../README.ko.md) | [인터페이스](handler-interfaces.ko.md) | [channel 샘플](../guide/samples/channel-messaging-samples.ko.md) | [SPOT](spring-boot-spot.ko.md) | [Registry](spring-boot-registry.ko.md)

# ZLink Framework Spring Boot Channel Messaging

## 현재 구현 기준

SPOT route를 받는 channel은 local `ROUTER` receive loop 안에서 core
`SpotRouteBridge` handoff를 함께 사용한다. 일반 channel packet은 기존 channel
dispatcher가 처리하고, SPOT relay packet만 bridge가 소비한다. outbound `DEALER`나
route mesh `ROUTER` socket은 channel runtime 소유이며, `SpotNode`에 직접 attach하지
않는다.

## 1. 목표

`Spring Boot` 애플리케이션 안에서 아래 경험을 제공하는 것이 목표다.

- channel 이름 기준 direct call
- bean으로 주입되는 공용 outbound client
- bean으로 주입되는 `ZLinkFanoutClient`와 `ZLinkRouteClient`
- annotation 기반 request/send handler
- HTTP controller 안에서도 같은 `ZLinkClient` 사용

## 2. 등록 방식

같은 역할은 자동 연결과 수동 연결 중 하나만 선택한다.

```java
@Configuration
@EnableZLinkFramework
public class ZLinkConfig implements ZLinkFrameworkConfigurer {
    @Override
    public void configure(ZLinkFrameworkOptions framework) {
        framework.addHandlersFromPackageOf(ZLinkConfig.class);

        framework.addClientServerChannel("api")
            .enableServer("tcp://0.0.0.0:7100")
            .addHandlerGroup("api");

        framework.addClientServerChannel("profile")
            .enableClient();

        framework.addClientServerChannel("account")
            .enableClient();

        framework.addFanoutChannel("profile-events")
            .enablePublisher("tcp://0.0.0.0:7200")
            .enableSubscriber();

        ZLinkDiscoveryBuilder discovery = framework.useDiscovery();
        discovery.addRegistryEndpoint("tcp://registry1:5551");
        discovery.addRegistryEndpoint("tcp://registry2:5551");
    }
}
```

수동 연결은 아래처럼 둔다.

```java
framework.addClientServerChannel("profile")
    .enableClient("tcp://10.0.10.15:7101");
```

앱 전체에서는 역할별로 방식을 나눠 쓸 수 있다.
예를 들어 `profile.client`는 discovery, `account.client`는 manual로 둘 수 있다.

중요한 점은 수동 연결이 `channel` 전체 설정이 아니라 `channel + capability`
설정이라는 점이다. 예를 들어 같은 `profile` channel이라도 `profile.client`와
`profile.subscriber`는 다른 연결 집합이다.

여기서 client manual 연결은 remote `RoutingId`를 따로 받지 않는다. channel client는
하부 `DEALER(client)`가 connect된 peer 집합으로 요청을 보내는 모델이므로,
startup과 런타임 제어 모두 endpoint 집합만 관리하면 된다.

manual 역할은 startup 시점에 endpoint 집합을 등록한다.

일반 `PUB/SUB` event publish는 `ZLinkFanoutClient` 같은 별도 surface로 설명한다.
이 표면도 `channel name + topic` 기준으로 동작한다.

## 3. Handler 모델

```java
@Component
public final class UserHandlers {
    private final ZLinkClient client;

    public UserHandlers(ZLinkClient client) {
        this.client = client;
    }

    @ZLinkRequest
    public GetUserReply getUser(
        GetUserRequest request,
        ZLinkRequestContext context) {
        GetAccountReply account = client.requestToChannel(
            "account",
            new GetAccountRequest(request.accountId())
        ).submit(GetAccountReply.class).toCompletableFuture().join();
        return new GetUserReply(request.accountId(), account.nickname());
    }
}
```

기본 packet key는 `GetUserRequest` 같은 payload 타입 이름을 쓴다.
외부 계약 때문에 다른 이름이 필요할 때만 annotation 또는 options에서 override한다.

## 4. Dispatch 기준

- 일반 request/send handler dispatch는 local `ROUTER(server)`가 받은 메시지 기준이다.
- outbound `DEALER(client)`가 받은 메시지는 reply correlation 경로로 본다.
- 따라서 `ROUTER -> DEALER` 임의 push는 현재 channel messaging 공용 계약에 넣지 않는다.

등록된 request handler 가 없거나 request payload decode, handler 실행 중 예외, invalid request frame 이
발생하면 server runtime 은 error reply 를 반환한다. 같은 사건은 Error 로그, counter, 전역
`ZLinkMessageDispatchErrorObserver` event 로도 남긴다.

send 또는 publish 에서 handler 를 찾지 못하면 reply 를 만들지 않고 drop 한다. send 는 Warning 로그와
counter, publish 는 Debug 로그 또는 counter 와 observer event 를 남긴다. observer 가 없더라도 기본
로그와 counter 는 생략하지 않는다. observer callback 실패는 dispatch 결과를 바꾸지 않는다.

## 5. Outbound-only 앱

local handler 없이 client만 쓰는 앱도 가능해야 한다.

```java
@Configuration
@EnableZLinkFramework
public class OutboundOnlyConfig implements ZLinkFrameworkConfigurer {
    @Override
    public void configure(ZLinkFrameworkOptions framework) {
        framework.addClientServerChannel("profile")
            .enableClient();
        framework.useDiscovery()
            .addRegistryEndpoint("tcp://registry1:5551");
    }
}
```

이 경우 local `ROUTER(server)`는 열지 않고 outbound `DEALER(client)`만 만든다.

## 6. Route mesh

Java framework도 `.NET`과 같이 channel을 세 종류로 나눈다.

| Builder | 용도 |
|---------|------|
| `addClientServerChannel(...)` | 일반 server/client request-send |
| `addFanoutChannel(...)` | pub/sub fanout |
| `addRouteMesh(...)` | target node `RoutingId`를 지정하는 routed channel |

route mesh는 session actor relay를 대체하지 않는다. application이 특정 node로
route send/request를 보내야 할 때 쓴다. 같은 runtime 안의 local managed actor
binding은 framework 내부 dispatch를 사용하고, remote actor binding은 stream node의

```java
RouteMeshChannelBuilder route = framework.addRouteMesh("play-route")
    .enableClient();
route.setRoutingId(RoutingId.from("play-node"));
```

route mesh는 server 역할과 client 역할을 따로 선언한다. 들어오는 route handler나
SPOT route ingress를 받아야 하는 runtime은 `enableServer(endpoint)`로 local ROUTER
endpoint를 연다. 다른 node로만 request/send를 보내는 runtime은 bind endpoint 없이
`enableClient()`를 선언하고 Discovery로 peer를 찾거나, `enableClient(endpoint)`로
수동 peer에 연결한다.

Registry-backed Spot remote address 기본 구현을 쓰려면 route mesh channel이 필요하다.
route mesh channel이 둘 이상이면 `useRegistrySpotRemoteAddresses(...)`에서 router
channel id를 명시해야 한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework Java SPOT Samples](../guide/samples/spot-samples.ko.md) | [다음: ZLink Framework Spring Boot Monitoring](spring-boot-monitoring.ko.md)
<!-- framework-adapter-nav:bottom:end -->
