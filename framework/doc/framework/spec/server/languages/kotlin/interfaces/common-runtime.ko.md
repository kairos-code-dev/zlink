# Kotlin 공통 runtime 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Java 공통 runtime](../../java/interfaces/common-runtime.ko.md)

Kotlin은 Java의 `ZLinkMeshNodeState`, `ZLinkFrameworkRuntimeState`, termination
intent·outcome·reason·result와 `ZLinkFrameworkRuntime`을 그대로 사용한다. 같은 enum, result wrapper와
runtime facade를 추가하지 않는다. Host-level `drain`과 `awaitDrained`는 deprecated `Shutdown` facade이고,
MeshName을 받는 partial termination member는 없다. Kotlin은 이 Java member를 그대로 사용한다.
Host가 continuity preflight를 통과해 relocation unit을 준비하는 동안에는 Java enum의 `RETIRING(2)`를
관측하며, admission을 seal한 뒤에는 `DRAINING(3)`으로 전환한다.

## Kotlin source signature

```kotlin
public suspend fun <T> CompletionStage<T>.await(): T
```

이 함수는 Java `CompletionStage`의 성공 값과 실패 원인을 보존한다. Coroutine 취소는 대기 중인 continuation만
끝내며 이미 시작한 shared host operation을 취소하지 않는다.

User Spot은 Spot과 current member Actor 전체를 bounded aggregate로 이전하므로 존재 자체가 Retire blocker가
아니다. `Disabled`, target 부재와 state capability 불일치의 termination reason은 Java와 동일하다.

Java와 같이 모든 target을 `Prepared`로 만들고 `Draining` descriptor를 publish하기 전 deadline은 durable abort와
source normalization 뒤 `Blocked/DeadlineExceeded`다. `Draining` publish 뒤 deadline은 source로 rollback하지
않고 bounded teardown과 recovery handoff 뒤 `ForceStopped/DeadlineExceeded`다. Kotlin enum이나 result를
추가하지 않는다.

```kotlin
val result = frameworkRuntime.retire(Duration.ofSeconds(30)).await()
val stopped = frameworkRuntime.shutdown().await()
```

별도 `retireAsync`, `shutdownAsync`, `drain` 또는 `awaitStopped` extension은 없다. Deprecated host drain은
`ZLinkTerminationResult`를 반환하며 `CompletionStage.await()`로 기다린다.

## Exact generated JVM signature

아래 JVM signature는 Kotlin source contract의 generated form이다.

```java
public final class systems.zlink.framework.kotlin.ZLinkCoroutineTurnAwaitKt {
  public static final <T> java.lang.Object await(java.util.concurrent.CompletionStage<T>, kotlin.coroutines.Continuation<? super T>);
}
```
