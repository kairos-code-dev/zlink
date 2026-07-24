# Java Channel messaging 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Channel messaging](../../../11-channel-messaging.ko.md)

ChannelName은 process 안의 송신 경로를 선택한다. RouteMesh·ClientServer·fanout builder와 typed handler,
client call의 정확한 payload type은 공통 계약의 역할 구분을 Java generic으로 투영한다. One-way operation은
`CompletionStage<Void> submit()` 하나를 제공하고, request operation의 `submit(...)`은 terminal reply까지
기다린다. One-way stage의 정상 완료는 local queue admission 완료를 뜻하며 별도 결과를 만들지 않는다.

```java
public interface ZLinkMeshChannelBuilder {
    ZLinkMeshChannelClientBuilder client();
    ZLinkMeshChannelServerBuilder server();
}

public interface ZLinkMeshChannelClientBuilder {}

public interface ZLinkMeshChannelServerBuilder {
    ZLinkMeshChannelServerBuilder setWeight(int weight);
    ZLinkMeshChannelServerBuilder addHandlerGroup(String groupName);
    <THandler extends ZLinkSendHandler<TMessage>, TMessage>
    ZLinkMeshChannelServerBuilder addSendHandler(
        Class<THandler> handlerType, Class<TMessage> messageType);
    <THandler extends ZLinkRequestHandler<TRequest, TReply>, TRequest, TReply>
    ZLinkMeshChannelServerBuilder addRequestHandler(
        Class<THandler> handlerType,
        Class<TRequest> requestType,
        Class<TReply> replyType);
}

public interface ClientServerChannelBuilder {
    ZLinkClientServerChannelClientBuilder client();
    ZLinkClientServerChannelServerBuilder server();
}

public interface ZLinkClientServerChannelClientBuilder {
    ZLinkClientServerChannelClientBuilder connect(String endpoint);
}

public interface ZLinkClientServerChannelServerBuilder {
    ZLinkClientServerChannelServerBuilder listen();
    ZLinkClientServerChannelServerBuilder listen(int port);
    ZLinkClientServerChannelServerBuilder setBindHost(String host);
    ZLinkClientServerChannelServerBuilder setAdvertiseHost(String host);
    ZLinkClientServerChannelServerBuilder setWeight(int weight);
    ZLinkClientServerChannelServerBuilder addHandlerGroup(String groupName);
    <THandler extends ZLinkSendHandler<TMessage>, TMessage>
    ZLinkClientServerChannelServerBuilder addSendHandler(
        Class<THandler> handlerType, Class<TMessage> messageType);
    <THandler extends ZLinkRequestHandler<TRequest, TReply>, TRequest, TReply>
    ZLinkClientServerChannelServerBuilder addRequestHandler(
        Class<THandler> handlerType,
        Class<TRequest> requestType,
        Class<TReply> replyType);
}

public interface ZLinkClient {
    ZLinkSendCall sendToChannel(String channelName, Object message);
    ZLinkRequestCall requestToChannel(String channelName, Object request);
}

public interface ZLinkRouteClient {
    ZLinkSendCall sendToNode(
        String meshName, RoutingId target, Object message);
    ZLinkRequestCall requestToNode(
        String meshName, RoutingId target, Object request);
    ZLinkSendCall sendToChannel(String channelName, Object message);
    ZLinkRequestCall requestToChannel(String channelName, Object request);
    ZLinkSpotSendCall sendToSpot(String spotId, Object message);
    ZLinkSpotRequestCall requestToSpot(String spotId, Object request);
}

public interface ZLinkFanoutClient {
    ZLinkFanoutPublishCall publish(
        String channelName, String topic, Object event);
    ZLinkFanoutPublishCall publish(String channelName, Object event);
}

public enum ZLinkRequestFailureReason {
    TIMEOUT, CANCELLED, SHUTDOWN
}

public final class ZLinkRequestFailureException extends RuntimeException {
    public ZLinkRequestFailureException(
        ZLinkRequestFailureReason reason, String message);
    public ZLinkRequestFailureException(
        ZLinkRequestFailureReason reason, String message, Throwable cause);
    public ZLinkRequestFailureReason reason();
}

```

Channel 호출은 process-local ChannelName index만 사용한다. 호출자가 MeshName과 ChannelName을 함께 넘겨
물리 배선을 고르는 overload는 제공하지 않는다. `sendToNode(String, RoutingId, Object)`는 exact RID를
지정하므로 첫 인자를 MeshName으로 해석한다.

Spot direct operation은 global SpotId만 address로 받고 Spot 전용 fluent call을 반환한다. 이 call의
`instanceSpot()` marker와 optional stable type·최초 Mesh는 Missing Instance Spot의 cold activation intent를 표현한다. Marker가
없으면 Missing authority를 not-found로 끝낸다. Existing authority는 저장된 kind·stable type과 current owner를
사용하므로 type이나 Mesh를 다시 요구하지 않는다. 세부 member와 cold activation 선택 규칙은
[Java Spot 인터페이스](spots.ko.md)가 소유한다.

`ZLinkRequestCall.yield(...)`는 `requestToChannel`이 만든 request에만 적용하며 submit을 먼저 호출하는
default method가 아니다. Runtime은 `SPOT_WIDE` User Spot 또는 Instance Spot application handler인지
operation submission 전에 검증한다. Entry Spot·Entry Actor·`PER_ACTOR`·Node·Channel·owner context 밖이면
outbound admission과 queue mutation 전에 `InvalidConfiguration`으로 완료한다. 현재 Spot gate가 필요한
target을 기다리는 일반 `submit(...)`도 같은 시점에 거부한다. One-way `submit()`은 FIFO queue admission을
유지하고 application handler를 inline 또는 reentrant하게 호출하지 않는다.

`channel(channelName)`이 반환하는 RouteMesh builder에서는 `client()` 또는 `server()`를 정확히 한 번
선택한다. 한 process에는 서로 다른 ClientServer ChannelName을 여러 개 등록할 수 있다.
`addClientServerChannel(channelName)`의 builder에서는 두 역할 중 하나 또는 둘 다 등록할 수
있지만 각 역할은 최대 한 번만 등록한다. 같은 ChannelName의 Client와 Server는 하나의 ClientServer
topology를 공유하지만 `(ChannelName, Role)` key의 별도 registration이다. 같은 역할의 중복 등록은 startup
오류다. RouteMesh ChannelName 충돌 규칙은 그대로 유지한다. 역할을 선택하기 전에는 weight와 handler를
설정할 수 없으며, Server 설정은 각 Server builder에만 존재한다.

RouteMesh Channel Server, ClientServer Server와 node-wide placement의 public weight는 signed `int`
`0..10000`이고 기본값은 `100`이다. `1..10000`은 eligible target 사이의 상대적 선택 비중이다. Startup과
runtime 변경에서 범위 밖 값은 configuration error다.
Runtime 변경은 descriptor revision으로 순서화하고 이미 제출했거나 reservation을 완료한 operation에는
적용하지 않는다. Readiness, drain과 capacity를 먼저 적용한 뒤 positive weight 합계를 최소 64-bit integer로
계산한다. Logical Multicast는 positive RouteMesh member를 각각 한 번만 포함하며 weight 크기로 제출 횟수를
늘리지 않는다.
Logical Multicast의 remote target은 source에서 고정한 MeshNode route의 local transport queue에 한 번씩
제출하고, local target은 일치하는 local Spot queue에 한 번씩 제출한다. Target별 성공·drop·unreachable
개수는 monitoring metric과 runtime event로 기록하며 public 결과로 반환하지 않는다. Remote Spot queue
수락과 remote·local handler 실행 또는 완료는 `CompletionStage` 완료 조건이 아니다.

Queue가 가득 차면 operation family의 send timeout까지 공간을 기다린다. Timeout, cancellation,
`ROUTE_NOT_CONNECTED`와 runtime 종료는 exceptional completion으로 전달한다. Target 부재는 operation별
기존 kind를 사용한다. Actor는 `ACTOR_ROUTE_NOT_FOUND`, Spot은 `SPOT_ROUTE_NOT_FOUND`, Mesh는
`MESH_NOT_FOUND`, request는 `REQUEST_TARGET_NOT_FOUND`, bound session과 Session Actor mapping은
`ACTOR_SESSION_NOT_BOUND`를 사용한다. Runtime
종료는 모든 one-way operation에서 `RUNTIME_SHUTDOWN=36`이다. `Backpressured`는 내부 대기 상태이며 public
terminal 결과가 아니다.

ClientServer의 local Server도 listener와 service admission을 마친 뒤 remote Server와 같은 candidate
집합에 포함한다. Ready, positive weight, non-draining 조건을 동일하게 적용하며 local 우선순위나 remote
제외 규칙은 없다. Local Server를 선택해도 Client DEALER에서 Server ROUTER로 실제 transport message를
전달하며 codec, HWM, timeout, cancellation, correlation과 terminal completion을 우회하지 않는다.

`ZLinkFanoutClient.publish(...)`에 내부 liveness용 exact topic byte `01 5A 4C 46 31`을 명시하면 transport를
시작하지 않고 `ZLinkConfigurationException`을 발생시킨다. Topic을 생략한 overload는 typed event의 packet
name을 사용하므로 이 내부 topic을 만들지 않는다.

Framework는 public call 뒤에서 Java binding의 exported raw socket API만 사용한다. Core service object,
private dispatch record와 native handle은 handler context, call result나 exception payload에 노출하지 않는다.

## Exact public member inventory

아래 선언은 이 category의 Java public type과 member를 고정한다.

```java
public final class systems.zlink.framework.channels.ZLinkRequestFailureReason extends java.lang.Enum<systems.zlink.framework.channels.ZLinkRequestFailureReason> {
  public static final systems.zlink.framework.channels.ZLinkRequestFailureReason TIMEOUT;
  public static final systems.zlink.framework.channels.ZLinkRequestFailureReason CANCELLED;
  public static final systems.zlink.framework.channels.ZLinkRequestFailureReason SHUTDOWN;
  public static systems.zlink.framework.channels.ZLinkRequestFailureReason[] values();
  public static systems.zlink.framework.channels.ZLinkRequestFailureReason valueOf(java.lang.String);
}
public final class systems.zlink.framework.channels.ZLinkRequestFailureException extends java.lang.RuntimeException {
  public systems.zlink.framework.channels.ZLinkRequestFailureException(systems.zlink.framework.channels.ZLinkRequestFailureReason, java.lang.String);
  public systems.zlink.framework.channels.ZLinkRequestFailureException(systems.zlink.framework.channels.ZLinkRequestFailureReason, java.lang.String, java.lang.Throwable);
  public systems.zlink.framework.channels.ZLinkRequestFailureReason reason();
}
public interface systems.zlink.framework.configuration.ZLinkMeshChannelBuilder {
  public abstract systems.zlink.framework.configuration.ZLinkMeshChannelClientBuilder client();
  public abstract systems.zlink.framework.configuration.ZLinkMeshChannelServerBuilder server();
}
public interface systems.zlink.framework.configuration.ZLinkMeshChannelClientBuilder {
}
public interface systems.zlink.framework.configuration.ZLinkMeshChannelServerBuilder {
  public abstract systems.zlink.framework.configuration.ZLinkMeshChannelServerBuilder setWeight(int);
  public abstract systems.zlink.framework.configuration.ZLinkMeshChannelServerBuilder addHandlerGroup(java.lang.String);
  public abstract <THandler extends systems.zlink.framework.channels.ZLinkSendHandler<TMessage>, TMessage> systems.zlink.framework.configuration.ZLinkMeshChannelServerBuilder addSendHandler(java.lang.Class<THandler>, java.lang.Class<TMessage>);
  public abstract <THandler extends systems.zlink.framework.channels.ZLinkRequestHandler<TRequest, TReply>, TRequest, TReply> systems.zlink.framework.configuration.ZLinkMeshChannelServerBuilder addRequestHandler(java.lang.Class<THandler>, java.lang.Class<TRequest>, java.lang.Class<TReply>);
}
public interface systems.zlink.framework.configuration.ZLinkClientServerChannelClientBuilder {
  public abstract systems.zlink.framework.configuration.ZLinkClientServerChannelClientBuilder connect(java.lang.String);
}
public interface systems.zlink.framework.configuration.ClientServerChannelBuilder {
  public abstract systems.zlink.framework.configuration.ZLinkClientServerChannelClientBuilder client();
  public abstract systems.zlink.framework.configuration.ZLinkClientServerChannelServerBuilder server();
}
public interface systems.zlink.framework.configuration.ZLinkClientServerChannelServerBuilder {
  public abstract systems.zlink.framework.configuration.ZLinkClientServerChannelServerBuilder listen();
  public abstract systems.zlink.framework.configuration.ZLinkClientServerChannelServerBuilder listen(int);
  public abstract systems.zlink.framework.configuration.ZLinkClientServerChannelServerBuilder setBindHost(java.lang.String);
  public abstract systems.zlink.framework.configuration.ZLinkClientServerChannelServerBuilder setAdvertiseHost(java.lang.String);
  public abstract systems.zlink.framework.configuration.ZLinkClientServerChannelServerBuilder setWeight(int);
  public abstract systems.zlink.framework.configuration.ZLinkClientServerChannelServerBuilder addHandlerGroup(java.lang.String);
  public abstract <THandler extends systems.zlink.framework.channels.ZLinkSendHandler<TMessage>, TMessage> systems.zlink.framework.configuration.ZLinkClientServerChannelServerBuilder addSendHandler(java.lang.Class<THandler>, java.lang.Class<TMessage>);
  public abstract <THandler extends systems.zlink.framework.channels.ZLinkRequestHandler<TRequest, TReply>, TRequest, TReply> systems.zlink.framework.configuration.ZLinkClientServerChannelServerBuilder addRequestHandler(java.lang.Class<THandler>, java.lang.Class<TRequest>, java.lang.Class<TReply>);
}
public interface systems.zlink.framework.channels.ZLinkClient {
  public abstract systems.zlink.framework.channels.ZLinkSendCall sendToChannel(java.lang.String, java.lang.Object);
  public abstract systems.zlink.framework.channels.ZLinkRequestCall requestToChannel(java.lang.String, java.lang.Object);
}
public interface systems.zlink.framework.channels.ZLinkFanoutClient {
  public abstract systems.zlink.framework.channels.ZLinkFanoutPublishCall publish(java.lang.String, java.lang.String, java.lang.Object);
  public abstract systems.zlink.framework.channels.ZLinkFanoutPublishCall publish(java.lang.String, java.lang.Object);
}
public interface systems.zlink.framework.channels.ZLinkFanoutPublishCall {
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> submit();
}
public interface systems.zlink.framework.channels.ZLinkMeshChannelRuntimeOptions {
  public abstract int weight();
  public abstract void weight(int);
}
public interface systems.zlink.framework.channels.ZLinkPublishCall {
  public default systems.zlink.framework.channels.ZLinkPublishCall metadata(java.lang.String, java.lang.String);
  public default systems.zlink.framework.channels.ZLinkPublishCall metadata(java.util.Map<java.lang.String, java.lang.String>);
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> submit();
}
public interface systems.zlink.framework.channels.ZLinkPublishContext extends systems.zlink.framework.ZLinkHandlerContext {
  public abstract java.lang.String topic();
  public abstract java.util.Optional<java.lang.String> source();
}
public interface systems.zlink.framework.channels.ZLinkPublishHandler<TMessage> {
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> handle(TMessage, systems.zlink.framework.channels.ZLinkPublishContext);
}
public interface systems.zlink.framework.channels.ZLinkRequestCall {
  public default systems.zlink.framework.channels.ZLinkRequestCall metadata(java.lang.String, java.lang.String);
  public default systems.zlink.framework.channels.ZLinkRequestCall metadata(java.util.Map<java.lang.String, java.lang.String>);
  public abstract systems.zlink.framework.channels.ZLinkRequestCall timeout(java.time.Duration);
  public abstract <TReply> java.util.concurrent.CompletionStage<TReply> submit(java.lang.Class<TReply>);
  public abstract <TReply> java.util.concurrent.CompletionStage<TReply> yield(java.lang.Class<TReply>);
}
public interface systems.zlink.framework.channels.ZLinkRequestContext extends systems.zlink.framework.ZLinkHandlerContext {
}
public interface systems.zlink.framework.channels.ZLinkRequestHandler<TRequest, TReply> {
  public abstract java.util.concurrent.CompletionStage<TReply> handle(TRequest, systems.zlink.framework.channels.ZLinkRequestContext);
}
public interface systems.zlink.framework.channels.ZLinkRouteClient {
  public abstract systems.zlink.framework.channels.ZLinkSendCall sendToChannel(java.lang.String, java.lang.Object);
  public abstract systems.zlink.framework.channels.ZLinkRequestCall requestToChannel(java.lang.String, java.lang.Object);
  public abstract systems.zlink.framework.channels.ZLinkSendCall sendToNode(java.lang.String, systems.zlink.contracts.core.RoutingId, java.lang.Object);
  public abstract systems.zlink.framework.spots.ZLinkSpotSendCall sendToSpot(java.lang.String, java.lang.Object);
  public abstract systems.zlink.framework.channels.ZLinkRequestCall requestToNode(java.lang.String, systems.zlink.contracts.core.RoutingId, java.lang.Object);
  public abstract systems.zlink.framework.spots.ZLinkSpotRequestCall requestToSpot(java.lang.String, java.lang.Object);
}
public interface systems.zlink.framework.channels.ZLinkRouteMeshRuntimeOptions {
  public abstract systems.zlink.framework.channels.ZLinkMeshPlacementRuntimeOptions mesh(java.lang.String);
  public abstract systems.zlink.framework.channels.ZLinkMeshChannelRuntimeOptions channel(java.lang.String);
}
public interface systems.zlink.framework.channels.ZLinkMeshPlacementRuntimeOptions {
  public abstract int placementWeight();
  public abstract void setPlacementWeight(int);
}
public interface systems.zlink.framework.channels.ZLinkRouteRequestContext extends systems.zlink.framework.ZLinkHandlerContext {
  public abstract java.lang.String meshName();
  public abstract systems.zlink.contracts.core.RoutingId sourceNodeRid();
}
public interface systems.zlink.framework.channels.ZLinkRouteRequestHandler<TRequest, TReply> {
  public abstract java.util.concurrent.CompletionStage<TReply> handle(TRequest, systems.zlink.framework.channels.ZLinkRouteRequestContext);
}
public interface systems.zlink.framework.channels.ZLinkRouteSendContext extends systems.zlink.framework.ZLinkHandlerContext {
  public abstract java.lang.String meshName();
  public abstract systems.zlink.contracts.core.RoutingId sourceNodeRid();
}
public interface systems.zlink.framework.channels.ZLinkRouteSendHandler<TMessage> {
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> handle(TMessage, systems.zlink.framework.channels.ZLinkRouteSendContext);
}
public interface systems.zlink.framework.channels.ZLinkSendCall {
  public default systems.zlink.framework.channels.ZLinkSendCall metadata(java.lang.String, java.lang.String);
  public default systems.zlink.framework.channels.ZLinkSendCall metadata(java.util.Map<java.lang.String, java.lang.String>);
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> submit();
}
public interface systems.zlink.framework.channels.ZLinkSendContext extends systems.zlink.framework.ZLinkHandlerContext {
}
public interface systems.zlink.framework.channels.ZLinkSendHandler<TMessage> {
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> handle(TMessage, systems.zlink.framework.channels.ZLinkSendContext);
}
public interface systems.zlink.framework.handlers.ZLinkHandlerGroup extends java.lang.annotation.Annotation {
  public abstract java.lang.String value();
}
public interface systems.zlink.framework.handlers.ZLinkHandlerGroups extends java.lang.annotation.Annotation {
  public abstract systems.zlink.framework.handlers.ZLinkHandlerGroup[] value();
}
public interface systems.zlink.framework.handlers.ZLinkPacket extends java.lang.annotation.Annotation {
  public abstract java.lang.String value();
}
public interface systems.zlink.framework.handlers.ZLinkPublish extends java.lang.annotation.Annotation {
  public abstract java.lang.String packetName();
}
public interface systems.zlink.framework.handlers.ZLinkRequest extends java.lang.annotation.Annotation {
  public abstract java.lang.String packetName();
}
public interface systems.zlink.framework.handlers.ZLinkSend extends java.lang.annotation.Annotation {
  public abstract java.lang.String packetName();
}
public interface systems.zlink.framework.handlers.ZLinkSpotActorRequest extends java.lang.annotation.Annotation {
  public abstract java.lang.String packetName();
}
public interface systems.zlink.framework.handlers.ZLinkSpotActorSend extends java.lang.annotation.Annotation {
  public abstract java.lang.String packetName();
}
public interface systems.zlink.framework.handlers.ZLinkSpotRequest extends java.lang.annotation.Annotation {
  public abstract java.lang.String packetName();
}
public interface systems.zlink.framework.handlers.ZLinkSpotSubscription extends java.lang.annotation.Annotation {
  public abstract java.lang.String spotNodeName();
  public abstract java.lang.String topic();
}
public interface systems.zlink.framework.handlers.ZLinkSpotTimer extends java.lang.annotation.Annotation {
  public abstract java.lang.String name();
  public abstract long periodMillis();
}
public interface systems.zlink.framework.handlers.ZLinkStreamPacket extends java.lang.annotation.Annotation {
}
public interface systems.zlink.framework.handlers.ZLinkStreamRaw extends java.lang.annotation.Annotation {
}
```
