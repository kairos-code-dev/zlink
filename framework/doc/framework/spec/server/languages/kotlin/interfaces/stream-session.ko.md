# Kotlin STREAM session 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Java STREAM session](../../java/interfaces/stream-session.ko.md) ·
[session Actor dispatch](../../../31-session-actor-dispatch.ko.md)

Kotlin session lifecycle과 coroutine handler는 Java session 계약을 그대로 사용한다. Actor dispatch를 켜는
builder member는 `enableActorDispatch()`이며 MeshName 인자를 받지 않는다. Startup에는 object role이 Client
또는 Server인 Mesh와 Location Store가 필요하다. Global ActorId가 current authority와 Mesh를 결정한다.

Session bind는 exact `ActorRef`를 한 번 받는다. Local Actor instance나 ActorId만 받는 bind overload는 없다.
Bind 시 current mapping이 없으면 `ActorLocationStale`, generation이 다르면 `ActorGenerationStale`, pre-commit
seal 구간이면 `ActorMoving`이다. Framework는 hidden retry나 local fallback을 수행하지 않는다.

## Kotlin source signature

```kotlin
suspend fun ZLinkSessionActors.bindOrGetActor(
    actor: ActorRef,
): ZLinkSessionActor
```

## Exact generated JVM signature

```java
public final class systems.zlink.framework.kotlin.ZLinkFrameworkExtensionsKt {
  public static final java.lang.Object bindOrGetActor(systems.zlink.framework.streams.ZLinkSessionActors, systems.zlink.framework.actors.ActorRef, kotlin.coroutines.Continuation<? super systems.zlink.framework.streams.ZLinkSessionActor>);
}
```
