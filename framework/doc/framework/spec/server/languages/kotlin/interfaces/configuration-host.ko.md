# Kotlin 구성과 host 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Java 구성](../../java/interfaces/configuration-host.ko.md)

Kotlin은 Java `ZLinkFrameworkOptions`와 builder를 그대로 사용한다. `ApplicationVersion`은 Kotlin `Long`이며
음수를 허용하지 않는다. Service liveness는 application traffic과 무관하게 5초마다 probe를 보내고 같은
current connection의 matching ACK를 15초 안에 받아야 한다. 다른 inbound frame은 deadline을 충족하지 않는다.
공유 JVM runtime이 이 profile을 적용하고 Kotlin DSL option으로 다시 노출하지 않는다.

MeshNode의 `maxMessageSize`, `mailboxMessageBudget`와 `mailboxByteBudget`도 Java startup configuration을
그대로 사용하며 Kotlin runtime setter나 별도 DSL option으로 반복하지 않는다. 정규화한 message bound의
internal negotiation, `0`이 선택하는 유한 mailbox profile과 startup validation은 Java 계약과 같다.

Fully encoded MeshNode descriptor 1 MiB, Spot type·stateful object capability collection 각 1024개와 capability별
readable state contract ID 1024개 상한도 Java 계약을 그대로 적용한다. 공유 runtime은 완성된 descriptor를
socket bind 전에 원자적으로 검증하며 truncate·split·partial publish하지 않는다.

같은 인자를 Java builder로 전달하기만 하는 wrapper는 만들지 않는다. DSL은 receiver와 reified type으로
중복을 실제로 줄이는 경우에만 제공한다.

## Kotlin source signature

```kotlin
fun ZLinkFrameworkOptions.useCoroutineHandlers(dispatcher: CoroutineDispatcher)
fun ZLinkFrameworkOptions.useCoroutineHandlers(
    scope: CoroutineScope,
    dispatcher: CoroutineDispatcher,
)

inline fun ZLinkFrameworkOptions.configureDispatch(
    block: ZLinkDispatchOptions.() -> Unit,
): ZLinkDispatchOptions

fun ZLinkFrameworkOptions.configureStreamCompression(
    configure: ZLinkStreamCompressionBuilder.() -> Unit,
): ZLinkFrameworkOptions
```

`CoroutineScope`를 받는 overload는 application이 제공한 scope의 종료를 존중한다. Framework가 만든 callback
bridge가 별도 public scope 또는 executor 설정을 노출하지 않는다.

## Exact generated JVM signature

아래 JVM signature는 Kotlin source contract의 generated form이다.

```java
public final class systems.zlink.framework.kotlin.ZLinkCoroutineHandlerOptionsKt {
  public static final void useCoroutineHandlers(systems.zlink.framework.configuration.ZLinkFrameworkOptions, kotlinx.coroutines.CoroutineDispatcher);
  public static final void useCoroutineHandlers(systems.zlink.framework.configuration.ZLinkFrameworkOptions, kotlinx.coroutines.CoroutineScope, kotlinx.coroutines.CoroutineDispatcher);
}
public final class systems.zlink.framework.kotlin.ZLinkDispatchOptionsExtensionsKt {
  public static final systems.zlink.framework.configuration.ZLinkDispatchOptions configureDispatch(systems.zlink.framework.configuration.ZLinkFrameworkOptions, kotlin.jvm.functions.Function1<? super systems.zlink.framework.configuration.ZLinkDispatchOptions, kotlin.Unit>);
  public static final systems.zlink.framework.configuration.ZLinkDispatchOptions onMessageFlow(systems.zlink.framework.configuration.ZLinkDispatchOptions, kotlin.jvm.functions.Function1<? super systems.zlink.framework.configuration.ZLinkMessageFlowEvent, kotlin.Unit>);
}
public final class systems.zlink.framework.kotlin.ZLinkFrameworkExtensionsKt {
  public static final <TActor extends systems.zlink.framework.actors.ZLinkActor, TFactory extends systems.zlink.framework.actors.ZLinkActorFactory> systems.zlink.framework.configuration.ZLinkMeshNodeBuilder actorFactory(systems.zlink.framework.configuration.ZLinkMeshNodeBuilder, java.lang.String, systems.zlink.framework.actors.ZLinkTransferPolicy<TActor>);
  public static systems.zlink.framework.configuration.ZLinkMeshNodeBuilder actorFactory$default(systems.zlink.framework.configuration.ZLinkMeshNodeBuilder, java.lang.String, systems.zlink.framework.actors.ZLinkTransferPolicy, int, java.lang.Object);
  public static final <TInstance, TState, TAdapter extends systems.zlink.framework.actors.ZLinkTransferStateAdapter<TInstance, TState>> systems.zlink.framework.actors.ZLinkTransferPolicy<TInstance> snapshotTransfer(java.lang.String);
  public static final <TReply> java.lang.Object awaitReply(systems.zlink.framework.channels.ZLinkRequestCall, java.lang.Class<TReply>, kotlin.coroutines.Continuation<? super TReply>);
  public static final <TReply> java.lang.Object awaitReply(systems.zlink.framework.channels.ZLinkRequestCall, kotlin.coroutines.Continuation<? super TReply>);
  public static final <TReply> java.lang.Object yieldReply(systems.zlink.framework.channels.ZLinkRequestCall, java.lang.Class<TReply>, kotlin.coroutines.Continuation<? super TReply>);
  public static final <TReply> java.lang.Object yieldReply(systems.zlink.framework.channels.ZLinkRequestCall, kotlin.coroutines.Continuation<? super TReply>);
  public static final <TReply> java.lang.Object awaitReply(systems.zlink.framework.actors.ZLinkActorRequestCall, java.lang.Class<TReply>, kotlin.coroutines.Continuation<? super TReply>);
  public static final <TReply> java.lang.Object awaitReply(systems.zlink.framework.actors.ZLinkActorRequestCall, kotlin.coroutines.Continuation<? super TReply>);
  public static final <TReply> java.lang.Object yieldReply(systems.zlink.framework.actors.ZLinkActorRequestCall, java.lang.Class<TReply>, kotlin.coroutines.Continuation<? super TReply>);
  public static final <TReply> java.lang.Object yieldReply(systems.zlink.framework.actors.ZLinkActorRequestCall, kotlin.coroutines.Continuation<? super TReply>);
  public static final <TReply> java.lang.Object requestToActorAwait(systems.zlink.framework.actors.ZLinkActorClient, java.lang.String, systems.zlink.framework.actors.ActorRef, java.lang.Object, java.lang.Class<TReply>, kotlin.coroutines.Continuation<? super TReply>);
  public static final <TReply> java.lang.Object requestToActorAwait(systems.zlink.framework.actors.ZLinkActorClient, java.lang.String, systems.zlink.framework.actors.ActorRef, java.lang.Object, kotlin.coroutines.Continuation<? super TReply>);
  public static final java.lang.Object findActor(systems.zlink.framework.actors.ZLinkActorDirectory, java.lang.String, java.lang.String, kotlin.coroutines.Continuation<? super systems.zlink.framework.actors.ActorRef>);
  public static final systems.zlink.framework.actors.ActorRefSnapshot snapshot(systems.zlink.framework.actors.ActorRef);
  public static final systems.zlink.framework.actors.ActorRef actorRef(systems.zlink.framework.actors.ActorRefSnapshot);
  public static final java.lang.Object isPeerReady(systems.zlink.framework.locations.ZLinkLocationReadiness, java.lang.String, systems.zlink.framework.locations.ZLinkLocationRole, systems.zlink.contracts.core.RoutingId, kotlin.coroutines.Continuation<? super java.lang.Boolean>);
  public static java.lang.Object isPeerReady$default(systems.zlink.framework.locations.ZLinkLocationReadiness, java.lang.String, systems.zlink.framework.locations.ZLinkLocationRole, systems.zlink.contracts.core.RoutingId, kotlin.coroutines.Continuation, int, java.lang.Object);
  public static final java.lang.Object bindOrGetActor(systems.zlink.framework.streams.ZLinkSessionActors, systems.zlink.framework.actors.ActorRef, kotlin.coroutines.Continuation<? super systems.zlink.framework.streams.ZLinkSessionActor>);
  public static final java.lang.Object awaitJoinCallVoid(systems.zlink.framework.actors.ZLinkActorJoinCall, kotlin.coroutines.Continuation<? super systems.zlink.framework.actors.ZLinkActorJoinResult<java.lang.Void>>);
  public static final <TReply> java.lang.Object awaitJoinCall(systems.zlink.framework.actors.ZLinkActorJoinCall, java.lang.Class<TReply>, kotlin.coroutines.Continuation<? super systems.zlink.framework.actors.ZLinkActorJoinResult<TReply>>);
  public static final <TReply> java.lang.Object awaitJoinCallReified(systems.zlink.framework.actors.ZLinkActorJoinCall, kotlin.coroutines.Continuation<? super systems.zlink.framework.actors.ZLinkActorJoinResult<TReply>>);
  public static final java.lang.Object yieldJoinCallVoid(systems.zlink.framework.actors.ZLinkActorJoinCall, kotlin.coroutines.Continuation<? super systems.zlink.framework.actors.ZLinkActorJoinResult<java.lang.Void>>);
  public static final <TReply> java.lang.Object yieldJoinCall(systems.zlink.framework.actors.ZLinkActorJoinCall, java.lang.Class<TReply>, kotlin.coroutines.Continuation<? super systems.zlink.framework.actors.ZLinkActorJoinResult<TReply>>);
  public static final <TReply> java.lang.Object yieldJoinCallReified(systems.zlink.framework.actors.ZLinkActorJoinCall, kotlin.coroutines.Continuation<? super systems.zlink.framework.actors.ZLinkActorJoinResult<TReply>>);
  public static final <T> java.lang.Object yieldWorker(systems.zlink.framework.spots.ZLinkWorkerCall<T>, kotlin.coroutines.Continuation<? super T>);
  public static final <TMessage> systems.zlink.framework.channels.ZLinkSendCall send(systems.zlink.framework.channels.ZLinkClient, java.lang.String, TMessage);
  public static final <TReply> java.lang.Object request(systems.zlink.framework.channels.ZLinkClient, java.lang.String, systems.zlink.contracts.messaging.Message, kotlin.coroutines.Continuation<? super TReply>);
  public static final <TEvent> systems.zlink.framework.channels.ZLinkFanoutPublishCall publishToTopic(systems.zlink.framework.channels.ZLinkFanoutClient, java.lang.String, java.lang.String, TEvent);
  public static final <TEvent> systems.zlink.framework.channels.ZLinkFanoutPublishCall publishToTopic(systems.zlink.framework.channels.ZLinkFanoutClient, java.lang.String, TEvent);
  public static final <TMessage> systems.zlink.framework.channels.ZLinkSendCall send(systems.zlink.framework.channels.ZLinkRouteClient, java.lang.String, systems.zlink.contracts.core.RoutingId, TMessage);
  public static final <TMessage> systems.zlink.framework.channels.ZLinkSendCall send(systems.zlink.framework.channels.ZLinkRouteClient, java.lang.String, TMessage);
  public static final <TReply> java.lang.Object request(systems.zlink.framework.channels.ZLinkRouteClient, java.lang.String, systems.zlink.contracts.core.RoutingId, systems.zlink.contracts.messaging.Message, kotlin.coroutines.Continuation<? super TReply>);
  public static final <TReply> java.lang.Object request(systems.zlink.framework.channels.ZLinkRouteClient, java.lang.String, systems.zlink.contracts.messaging.Message, kotlin.coroutines.Continuation<? super TReply>);
  public static final <TMessage> systems.zlink.framework.channels.ZLinkSendCall send(systems.zlink.framework.channels.ZLinkRouteClient, systems.zlink.framework.spots.SpotHandle, TMessage);
  public static final <TReply> java.lang.Object request(systems.zlink.framework.channels.ZLinkRouteClient, systems.zlink.framework.spots.SpotHandle, systems.zlink.contracts.messaging.Message, kotlin.coroutines.Continuation<? super TReply>);
  public static final <TMessage> systems.zlink.framework.channels.ZLinkSendCall send(systems.zlink.framework.channels.ZLinkRouteClient, systems.zlink.framework.spots.InstanceSpotAddress, TMessage);
  public static final <TMessage> systems.zlink.framework.channels.ZLinkRequestCall request(systems.zlink.framework.channels.ZLinkRouteClient, systems.zlink.framework.spots.InstanceSpotAddress, TMessage);
  public static final <TMessage> systems.zlink.framework.channels.ZLinkSendCall send(systems.zlink.framework.spots.ZLinkSpotOutbound, systems.zlink.framework.spots.InstanceSpotAddress, TMessage);
  public static final <TMessage> systems.zlink.framework.channels.ZLinkRequestCall request(systems.zlink.framework.spots.ZLinkSpotOutbound, systems.zlink.framework.spots.InstanceSpotAddress, TMessage);
  public static final <TSpot extends systems.zlink.framework.spots.ZLinkSpot<?>> java.lang.Object create(systems.zlink.framework.spots.ZLinkSpotManager, java.lang.String, kotlin.coroutines.Continuation<? super systems.zlink.framework.spots.ZLinkSpotCreateResult>);
  public static final <TSpot extends systems.zlink.framework.spots.ZLinkSpot<?>> java.lang.Object create(systems.zlink.framework.spots.ZLinkSpotManager, java.lang.String, systems.zlink.framework.messaging.ZLinkMessage, kotlin.coroutines.Continuation<? super systems.zlink.framework.spots.ZLinkSpotCreateResult>);
  public static final <TSpot extends systems.zlink.framework.spots.ZLinkSpot<?>> java.lang.Object create(systems.zlink.framework.spots.ZLinkSpotManager, java.lang.String, systems.zlink.contracts.core.RoutingId, kotlin.coroutines.Continuation<? super systems.zlink.framework.spots.ZLinkSpotCreateResult>);
  public static final <TSpot extends systems.zlink.framework.spots.ZLinkSpot<?>> java.lang.Object getOrCreate(systems.zlink.framework.spots.ZLinkSpotManager, java.lang.String, systems.zlink.contracts.core.RoutingId, kotlin.coroutines.Continuation<? super systems.zlink.framework.spots.ZLinkSpotCreateResult>);
  public static final <TSpot extends systems.zlink.framework.spots.ZLinkSpot<?>> java.lang.Object getOrCreate(systems.zlink.framework.spots.ZLinkSpotManager, java.lang.String, systems.zlink.contracts.core.RoutingId, systems.zlink.framework.messaging.ZLinkMessage, kotlin.coroutines.Continuation<? super systems.zlink.framework.spots.ZLinkSpotCreateResult>);
  public static final systems.zlink.framework.configuration.ZLinkFrameworkOptions configureStreamCompression(systems.zlink.framework.configuration.ZLinkFrameworkOptions, kotlin.jvm.functions.Function1<? super systems.zlink.framework.configuration.ZLinkStreamCompressionBuilder, kotlin.Unit>);
}
```
