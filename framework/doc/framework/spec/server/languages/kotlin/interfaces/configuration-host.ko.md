# Kotlin 구성과 host 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Java 구성](../../java/interfaces/configuration-host.ko.md) ·
[MeshNode 공통 계약](../../../21-mesh-node.ko.md)

Kotlin application은 Java builder를 직접 사용한다. Kotlin DSL은 receiver와 reified type으로 실제 중복을
줄이는 경우에만 제공하며 Java contract에 없는 역할, factory default, allocation provider를 만들지 않는다.
따라서 ClientServer의 Client-only connect와 Server RID·lifecycle generation별 intent 통합, fanout의
Subscriber-only connect와 automatic·manual subscriber 혼합 금지는
[Java 구성](../../java/interfaces/configuration-host.ko.md)의 같은 계약을 그대로 적용한다.

Automatic RouteMesh는 RID를 canonical byte order로 비교하고 더 작은 RID의 MeshNode만 상대 endpoint로
connect한다. Manual topology는 application endpoint 구성에 따라 한쪽 또는 양쪽에서 connect할 수 있다.
양쪽 연결이나 automatic discovery 경합·오래된 snapshot으로 중복 후보가 생기면 handshake와 admission이
같은 RID와 lifecycle generation을 확인해 하나만 ready 상태로 유지한다.

MeshNode의 object role은 `None`, `Client`, `Server` 중 하나다. `objects()`를 호출하지 않으면 `None`,
`client()`는 outbound manager와 resolve를 제공하고 `server()`는 Client 기능과 factory·Entry registration을
함께 제공한다. Client와 Server는 Location Store가 필요하다. None에는 object manager나 factory가 없다.
한 node에서 role을 중복 선택하면 startup configuration error다.

`ZLinkFrameworkOptions.addLocationStore(...)`와 `addRelocationStore(...)`는 Java public member를 그대로 사용한다.
`RECREATE` 또는 `SNAPSHOT` factory가 하나라도 있거나 Instance Spot factory가 하나라도 있으면 Relocation Store를
정확히 하나 등록해야 하며 missing·duplicate는 socket bind 전에 configuration error다. Instance Spot factory가
없고 `DISABLED` factory와 same-node join만 사용하는 host에는 Relocation Store가 필수가 아니다. 두 capability를
묶는 Kotlin DSL이나 Redis 전용 registration helper는 제공하지 않는다.
완료 가능한 모든 cross-node Actor·Spot 이동은 Relocation Store를 사용한다. `RECREATE`도 accepted journal과 recovery
payload를 저장하며 `SNAPSHOT`은 application state를 추가로 저장한다. Same-node Actor join은 Relocation payload를
만들지 않고, `DISABLED` cross-node 이동은 capture 전에 거부한다.

다음 Java builder member는 Kotlin에서 property 변환 없이 같은 JVM signature로 직접 호출한다.

```java
public interface systems.zlink.framework.configuration.ZLinkMeshNodeBuilder {
  public abstract systems.zlink.framework.configuration.ZLinkMeshNodeBuilder setRoutingIdPrefix(java.lang.String);
  public abstract systems.zlink.framework.configuration.ZLinkMeshNodeBuilder setPlacementWeight(int);
  public abstract systems.zlink.framework.configuration.ZLinkMeshNodeBuilder setObjectCapacity(int, int);
  public abstract systems.zlink.framework.configuration.ZLinkMeshObjectRoleBuilder objects();
}
public interface systems.zlink.framework.configuration.ZLinkMeshObjectRoleBuilder {
  public abstract systems.zlink.framework.configuration.ZLinkMeshObjectClientBuilder client();
  public abstract systems.zlink.framework.configuration.ZLinkMeshObjectServerBuilder server();
}
public interface systems.zlink.framework.configuration.ZLinkStreamNodeBuilder {
  public abstract systems.zlink.framework.configuration.ZLinkStreamNodeBuilder enableActorDispatch();
}
```

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

inline fun <reified TActor, reified TFactory>
    ZLinkMeshObjectServerBuilder.actorFactory(
        actorType: String,
        placement: ZLinkObjectPlacementOptions?,
        relocation: ZLinkRelocationPolicy<TActor>,
    ): ZLinkMeshObjectServerBuilder
    where TActor : ZLinkActor,
          TFactory : ZLinkActorFactory
```

`placement`은 nullable이지만 `relocation`에는 default가 없다. Placement option은 placement profile collection,
type별 `maxActiveObjects`, `maxPendingActivations`만 가진다. Node default는 active 10000, pending 128이고
type override 범위는 1..`Int.MAX_VALUE`다. Effective capacity는 node와 type 값 중 작은 값이다. Node placement
weight는 0..100이고 기본값은 100이다. Channel weight와 별개이며 runtime update와 descriptor snapshot에
같은 값을 사용한다.

MeshNode와 Store-backed fanout publisher의 automatic RID는 `prefix-<32 lowercase hex>` 형식이다. Prefix는
ASCII `[A-Za-z0-9._-]` 1..64자이고 full RID는 UTF-8 255 bytes 이하다. Active owner 충돌은 새 suffix로 최대
8회 재시도하고 모두 충돌하면 `RoutingIdConflict`다. Fixed RID는 object role이나 automatic Store descriptor가
없는 manual topology에서만 사용할 수 있다. Slot count, allocation group과 public allocation provider는 없다.

모든 Actor, User Spot, Instance Spot factory는 stable type, optional typed placement와 명시적인
`Disabled`·`Recreate`·`Snapshot` policy를 받는다. Policy를 생략하는 Kotlin overload와 `$default` JVM member는
생성하지 않는다. Snapshot은 Java `ZLinkRelocationPolicy.snapshot(Adapter::class.java)`를 직접 사용한다. Actor
factory adapter는 `ZLinkActorRelocationAdapter`, User·Instance Spot factory adapter는
`ZLinkSpotRelocationAdapter`인지 socket bind 전에 검증한다. Kotlin 전용 policy, reified adapter registration과
suspending adapter를 추가하지 않는다. `ZLinkStreamNodeBuilder.enableActorDispatch()`는 인자가 없고 global ID가
Mesh를 결정한다.

`Recreate` 또는 `Snapshot` factory가 하나라도 있거나 Instance Spot factory가 하나라도 등록된 Object Server는
Java root의 `addRelocationStore(...)`로 Relocation Store를 정확히 하나 등록한다. Instance Spot factory가 없고
모든 factory가 `Disabled`인 same-node 구성만 이를 생략할 수 있다.

## Exact generated JVM signature

```java
public final class systems.zlink.framework.kotlin.ZLinkCoroutineHandlerOptionsKt {
  public static final void useCoroutineHandlers(systems.zlink.framework.configuration.ZLinkFrameworkOptions, kotlinx.coroutines.CoroutineDispatcher);
  public static final void useCoroutineHandlers(systems.zlink.framework.configuration.ZLinkFrameworkOptions, kotlinx.coroutines.CoroutineScope, kotlinx.coroutines.CoroutineDispatcher);
}
public final class systems.zlink.framework.kotlin.ZLinkDispatchOptionsExtensionsKt {
  public static final systems.zlink.framework.configuration.ZLinkDispatchOptions configureDispatch(systems.zlink.framework.configuration.ZLinkFrameworkOptions, kotlin.jvm.functions.Function1<? super systems.zlink.framework.configuration.ZLinkDispatchOptions, kotlin.Unit>);
}
public final class systems.zlink.framework.kotlin.ZLinkFrameworkExtensionsKt {
  public static final <TActor extends systems.zlink.framework.actors.ZLinkActor, TFactory extends systems.zlink.framework.actors.ZLinkActorFactory> systems.zlink.framework.configuration.ZLinkMeshObjectServerBuilder actorFactory(systems.zlink.framework.configuration.ZLinkMeshObjectServerBuilder, java.lang.String, systems.zlink.framework.configuration.ZLinkObjectPlacementOptions, systems.zlink.framework.actors.ZLinkRelocationPolicy<TActor>);
  public static final systems.zlink.framework.configuration.ZLinkFrameworkOptions configureStreamCompression(systems.zlink.framework.configuration.ZLinkFrameworkOptions, kotlin.jvm.functions.Function1<? super systems.zlink.framework.configuration.ZLinkStreamCompressionBuilder, kotlin.Unit>);
}
```
