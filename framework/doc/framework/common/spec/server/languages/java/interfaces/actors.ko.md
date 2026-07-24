# Java Actor 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Actor 공통 계약](../../../../22-actor-model.ko.md)

이 문서는 Java에서 Actor factory, context, messaging, manager와 relocation adapter를 표현하는 공개
인터페이스를 고정한다. 일반 message는 ActorId로 대상을 지정하고, 특정 incarnation을 변경하는
operation은 exact `ActorRef`를 사용한다.

```java
public interface ZLinkActorFactory {
    CompletionStage<ZLinkActor> create(String actorId, ZLinkActorContext context);
}

public interface ZLinkActorHandlerRegistry {
    void addHandler(Class<?> handlerType);
}

public interface ZLinkRelocationCancellation {
    boolean isCancellationRequested();
}

public interface ZLinkActorRelocationAdapter<TActor extends ZLinkActor> {
    CompletionStage<byte[]> capture(
        TActor actor, ZLinkRelocationCancellation cancellation);
    CompletionStage<Void> restore(
        TActor actor, byte[] state, ZLinkRelocationCancellation cancellation);
}

public sealed interface ZLinkRelocationPolicy<TInstance>
    permits ZLinkRelocationPolicy.Disabled,
            ZLinkRelocationPolicy.Recreate,
            ZLinkRelocationPolicy.Snapshot {
    record Disabled<T>() implements ZLinkRelocationPolicy<T> {}
    record Recreate<T>() implements ZLinkRelocationPolicy<T> {}
    record Snapshot<T>(Class<?> adapterClass)
        implements ZLinkRelocationPolicy<T> {}

    static <T> ZLinkRelocationPolicy<T> disabled() {
        return new Disabled<>();
    }
    static <T> ZLinkRelocationPolicy<T> recreate() {
        return new Recreate<>();
    }
    static <T> ZLinkRelocationPolicy<T> snapshot(Class<?> adapterClass) {
        return new Snapshot<>(adapterClass);
    }
}

```

[Factory](../../../../01-glossary.ko.md#factory) registration의 정확한 builder member는
[구성과 host](configuration-host.ko.md)가 소유한다. Cross-node relocation policy는 Actor factory 등록에
직접 연결한다. Runtime은 factory가 반환한 Actor를 명시한 `actorClass`로 검사해 type 불일치를 startup
오류로 반환한다. Factory와 분리된 relocation registry는 제공하지 않는다.
`Snapshot` Actor policy의 `adapterClass`는 해당 Actor type의
`ZLinkActorRelocationAdapter<TActor>`를 구현해야 한다. User·Instance Spot policy의 adapter type 검증은
[Spot 인터페이스](spots.ko.md)가 소유한다. `Class<?>`를 받는 것은 Java type erasure 때문에 policy value를
공통으로 유지하기 위한 표현이며, Framework는 factory type과 adapter generic target이 일치하는지 socket bind
전에 검사한다. Mismatch는 startup configuration error다.
`snapshot(null)`과 null adapter class를 가진 policy도 socket bind 전에 `InvalidConfiguration`으로 거부한다.

Actor adapter는 application state를 최대 64 MiB의 opaque `byte[]`로 capture·restore한다. Public state DTO, `TState`,
`stateContractId`, state class와 `ZLinkMessage`를 relocation surface에 두지 않는다. Framework는 capture가 정상
완료한 배열을 즉시 복사한다. Capture가 반환한 배열은 adapter가 계속 소유하며 completion 뒤 재사용하거나
변경해도 저장 payload가 바뀌지 않는다. Restore에는 호출마다 저장 payload의 fresh defensive copy를 전달하고
adapter는 stage가 끝난 뒤 그 배열을 보관하지 않는다. 길이가 0인 배열도 유효한 Snapshot state이며 `Recreate`로
해석하거나 Restore를 생략하지 않는다. Adapter는 owner claim, relocation envelope, generation과 recovery phase를
받지 않는다.

Cross-node materialization에서 Actor factory가 `Snapshot` policy를 사용하면 maintenance Actor relocation,
remote User·[Entry Spot](../../../../01-glossary.ko.md#entry-user-instance-spot) join과 whole User Spot relocation의 각 Actor participant에 같은 Actor adapter를 사용한다.
Same-node join과 `Disabled` policy에서는 adapter를 호출하지 않는다. `Recreate`는 application state를 capture하지
않으므로 adapter가 없다.

Target이 `Activated`에 도달해도 application과 session ingress는 sealed 상태를 유지하고 restore, accepted
journal replay와 bound-session route는 staged 상태로만 준비한다. Source cleanup이 terminal 상태에 도달하고
authority의 `COMPLETED` CAS가 성공한 뒤에만 target을 `READY`로 열고 relocation fence를 해제한다. `COMPLETED`
뒤의 target failure는 ordinary [owner](../../../../01-glossary.ko.md#owner) loss로 처리하며 이전 relocation을 transparent replay하지 않는다. 이
barrier를 조작하는 public phase API는 제공하지 않는다.

Target replacement가 발생하면 stable relocation 안의 각 attempt가 factory와 `restore(...)`를 at-least-once
호출할 수 있고 중단된 stale attempt callback이 successor와 겹칠 수 있다. `capture(...)`도 immutable relocation
root가 [authority](../../../../01-glossary.ko.md#authority)에 연결되기 전까지 반복될 수 있다. Current exact owner와 attempt fence만 completion을 commit하고
admission을 열 수 있다. Callback에는 relocation ID를 추가하지 않으므로 application restore와 capture는 retry-safe해야
하며 exactly-once external side effect를 보장하지 않는다. Factory는 target attempt마다 fresh Actor instance를
만들고 Framework는 그 attempt의 `restore(...)`만 해당 instance에 호출한다. Source instance나 이전 target
attempt의 instance를 새 attempt에 재사용하지 않으며 같은 attempt에서는 restore가 반복될 수 있다.

Capture stage가 exception으로 끝나면 authority publication 전에 attempt를 abort하고 source authority와 admission을
유지한다. Restore stage가 exception으로 끝나면 target admission을 sealed 상태로 유지하고 같은 immutable payload의
retry 또는 target replacement를 수행한다. Exception을 빈 payload나 정상 completion으로 바꾸지 않는다. Capture의
null stage와 null `byte[]`, Restore의 null stage는 adapter contract 위반이다. Host Retire에서 deadline이 먼저
확정되지 않은 precommit adapter exception과 contract 위반은 `Blocked/StateIncompatible`로 분류한다. [Deadline](../../../../01-glossary.ko.md#deadline)이
먼저 확정되면 `Blocked/DeadlineExceeded`를 사용하며 stale target attempt의 cancellation은 terminal result를
commit하지 못한다. Adapter는
반복 호출과 stale attempt overlap을 허용하도록 retry-safe해야 하며 callback 안의 외부 side effect를 exactly-once로
간주할 수 없다.

Relocated terminal reply accounting은 internal command ID 46 `replyRelayAck`를 사용한다. 이 command는 stable
relocation ID, operation ID, exact request-source fence(owner ID, lease generation, node RID, node generation)와
status만 가지며 payload와 metadata를 싣지 않는다. Physical connection close는 terminal 증거가 아니다. ACK 또는
accepted record에 저장한 exact request-source lease expiry만 terminal accounting을 완료하며 public ACK API는 없다.

Source는 connection-bound one-way를 포함해 admission한 모든 connection-bound work가 terminal accounting에
도달한 뒤에만 `CAPTURED`를 commit한다. Durable accepted journal은 exact owner lease가 있는 source에서만
사용한다. Pre-`CAPTURED` drain이 deadline 안에 끝나지 않으면 relocation을 abort하고 host Retire를
`BLOCKED/DEADLINE_EXCEEDED`로 끝낸다. Durable abort와 source normalization이 끝나기 전에 source admission을
열지 않는다. Connection-bound one-way를 미완료 상태로 capture하는 예외는 없다.

Standalone relocation의 Actor는 source Entry Spot member여야 한다. User Spot member Actor는 standalone relocation으로
분리하지 않고 Spot과 current member 전체를 bounded aggregate로 함께 옮긴다. User Spot membership 자체는 Retire
blocker가 아니며 participant 하나라도 `Disabled`이거나 호환 target을 확보할 수 없을 때만 aggregate 전체를
차단한다. `Disabled` participant는 `BLOCKED/RELOCATION_DISABLED`, target·capacity·reservation 부재는
`BLOCKED/TARGET_UNAVAILABLE`, application version·type·[Snapshot](../../../../01-glossary.ko.md#snapshot) adapter capability 불일치는
`BLOCKED/STATE_INCOMPATIBLE`다. Standalone Actor는 target factory와 restore를 끝내고 accepted journal을
application handler가 실행하지 않은 staging queue로 준비한 뒤 `NEW_OWNER` CAS를 수행한다. 이 CAS는
owner, authority owner generation과 current [Spot](../../../../01-glossary.ko.md#spot)을 target Entry identity로
원자적으로 바꾼다. Commit 뒤 target `onActorRelocated`와 source `onLeaveActor`를 호출하고 old Entry [membership](../../../../01-glossary.ko.md#membership)의
durable cleanup을 완료한 뒤 journal replay와 dispatch를 개방한다. Callback 실패는 commit을 rollback하거나 source
owner를 복원하지 않으며 callback을 retry한다.
Source process가 종료되면 durable source cleanup이 source callback 완료를 대신해 target recovery가 계속된다.
Lifecycle callback은 retry-safe해야 하며 at-least-once 호출될 수 있다. 이 순서를 제어하는 public phase API는 없다.

새 distributed Actor를 만들 때 Framework는 owner가 될 target 하나를 선택하고, 그 target에서
`CREATING` authority와 pending capacity를 하나의 reservation으로 함께 확보한다. Reservation을 확보한
target만 factory, initial Entry membership과 initialize를 수행한다. 성공하면 같은 reservation을 `READY`와
active capacity로 commit하고 실패하면 abort한다. CAS 경쟁에서 진 target은 별도 factory를 실행하지 않는다.

## Exact public member inventory

아래 선언은 이 category의 Java public type과 member를 고정한다.

```java
public interface systems.zlink.framework.actors.ZLinkActorFactory {
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.actors.ZLinkActor> create(java.lang.String, systems.zlink.framework.actors.ZLinkActorContext);
}
public interface systems.zlink.framework.actors.ZLinkRelocationCancellation {
  public abstract boolean isCancellationRequested();
}
public interface systems.zlink.framework.actors.ZLinkActorRelocationAdapter<TActor extends systems.zlink.framework.actors.ZLinkActor> {
  public abstract java.util.concurrent.CompletionStage<byte[]> capture(TActor, systems.zlink.framework.actors.ZLinkRelocationCancellation);
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> restore(TActor, byte[], systems.zlink.framework.actors.ZLinkRelocationCancellation);
}
public sealed interface systems.zlink.framework.actors.ZLinkRelocationPolicy<TInstance>
    permits systems.zlink.framework.actors.ZLinkRelocationPolicy.Disabled,
            systems.zlink.framework.actors.ZLinkRelocationPolicy.Recreate,
            systems.zlink.framework.actors.ZLinkRelocationPolicy.Snapshot {
  public static <T> systems.zlink.framework.actors.ZLinkRelocationPolicy<T> disabled();
  public static <T> systems.zlink.framework.actors.ZLinkRelocationPolicy<T> recreate();
  public static <T> systems.zlink.framework.actors.ZLinkRelocationPolicy<T> snapshot(java.lang.Class<?>);
}
public final class systems.zlink.framework.actors.ZLinkRelocationPolicy$Disabled<T> extends java.lang.Record implements systems.zlink.framework.actors.ZLinkRelocationPolicy<T> {
  public systems.zlink.framework.actors.ZLinkRelocationPolicy$Disabled();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
}
public final class systems.zlink.framework.actors.ZLinkRelocationPolicy$Recreate<T> extends java.lang.Record implements systems.zlink.framework.actors.ZLinkRelocationPolicy<T> {
  public systems.zlink.framework.actors.ZLinkRelocationPolicy$Recreate();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
}
public final class systems.zlink.framework.actors.ZLinkRelocationPolicy$Snapshot<T> extends java.lang.Record implements systems.zlink.framework.actors.ZLinkRelocationPolicy<T> {
  public systems.zlink.framework.actors.ZLinkRelocationPolicy$Snapshot(java.lang.Class<?>);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.Class<?> adapterClass();
}
public interface systems.zlink.framework.actors.ZLinkActorHandlerRegistry {
  public abstract void addHandler(java.lang.Class<?>);
}
public final class systems.zlink.framework.actors.ActorRef extends java.lang.Record {
  public systems.zlink.framework.actors.ActorRef(java.lang.String, long, java.lang.String, systems.zlink.contracts.core.RoutingId);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String actorId();
  public long objectGeneration();
  public java.lang.String meshName();
  public systems.zlink.contracts.core.RoutingId nodeRid();
}
public interface systems.zlink.framework.actors.ZLinkActor {
  public abstract java.lang.String actorId();
  public abstract systems.zlink.framework.actors.ZLinkActorContext context();
  public default void configure();
}
public interface systems.zlink.framework.actors.ZLinkActorClient {
  public abstract systems.zlink.framework.actors.ZLinkActorSendCall sendToActor(java.lang.String, java.lang.Object);
  public abstract systems.zlink.framework.actors.ZLinkActorRequestCall requestToActor(java.lang.String, java.lang.Object);
}
public interface systems.zlink.framework.actors.ZLinkActorContext {
  public abstract java.lang.String meshName();
  public abstract java.util.Optional<java.lang.String> spotId();
  public abstract systems.zlink.framework.actors.ZLinkBoundSession boundSession();
  public abstract systems.zlink.framework.actors.ZLinkActorJoinCall joinSpot(java.lang.String, java.lang.Object);
  public abstract systems.zlink.framework.actors.ZLinkActorJoinCall joinEntrySpot(java.lang.Object);
}
public interface systems.zlink.framework.actors.ZLinkActorJoinCall {
  public abstract systems.zlink.framework.actors.ZLinkActorJoinCall timeout(java.time.Duration);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.actors.ZLinkActorJoinResult<java.lang.Void>> submit();
  public abstract <TReply> java.util.concurrent.CompletionStage<systems.zlink.framework.actors.ZLinkActorJoinResult<TReply>> submit(java.lang.Class<TReply>);
}
public final class systems.zlink.framework.actors.ZLinkActorJoinResult$Accepted<TReply> extends java.lang.Record implements systems.zlink.framework.actors.ZLinkActorJoinResult<TReply> {
  public systems.zlink.framework.actors.ZLinkActorJoinResult$Accepted(systems.zlink.framework.actors.ActorRef, TReply);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.actors.ActorRef actor();
  public TReply reply();
}
public final class systems.zlink.framework.actors.ZLinkActorJoinResult$Rejected<TReply> extends java.lang.Record implements systems.zlink.framework.actors.ZLinkActorJoinResult<TReply> {
  public systems.zlink.framework.actors.ZLinkActorJoinResult$Rejected(TReply);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public TReply reply();
}
public sealed interface systems.zlink.framework.actors.ZLinkActorJoinResult<TReply>
    permits systems.zlink.framework.actors.ZLinkActorJoinResult.Accepted,
            systems.zlink.framework.actors.ZLinkActorJoinResult.Rejected {
  public abstract TReply reply();
}
public interface systems.zlink.framework.actors.ZLinkActorManager {
  public abstract systems.zlink.framework.actors.ZLinkActorCreateCall create(java.lang.String, java.lang.String);
  public abstract systems.zlink.framework.actors.ZLinkActorGetOrCreateCall getOrCreate(java.lang.String, java.lang.String);
  public abstract java.util.concurrent.CompletionStage<java.util.Optional<systems.zlink.framework.actors.ActorRef>> find(java.lang.String);
  public abstract java.util.concurrent.CompletionStage<java.util.Optional<systems.zlink.framework.spots.SpotRef>> findSpot(java.lang.String);
  public abstract java.util.concurrent.CompletionStage<java.lang.Boolean> destroy(systems.zlink.framework.actors.ActorRef);
}
public interface systems.zlink.framework.actors.ZLinkActorCreateCall {
  public abstract systems.zlink.framework.actors.ZLinkActorCreateCall inMesh(java.lang.String);
  public abstract systems.zlink.framework.actors.ZLinkActorCreateCall request(java.lang.Object);
  public abstract systems.zlink.framework.actors.ZLinkActorCreateCall request(systems.zlink.framework.messaging.ZLinkMessage);
  public abstract systems.zlink.framework.actors.ZLinkActorCreateCall timeout(java.time.Duration);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.actors.ZLinkActorCreateResult> submit();
}
public interface systems.zlink.framework.actors.ZLinkActorGetOrCreateCall {
  public abstract systems.zlink.framework.actors.ZLinkActorGetOrCreateCall inMesh(java.lang.String);
  public abstract systems.zlink.framework.actors.ZLinkActorGetOrCreateCall request(java.lang.Object);
  public abstract systems.zlink.framework.actors.ZLinkActorGetOrCreateCall request(systems.zlink.framework.messaging.ZLinkMessage);
  public abstract systems.zlink.framework.actors.ZLinkActorGetOrCreateCall timeout(java.time.Duration);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.actors.ZLinkActorCreateResult> submit();
}
public sealed interface systems.zlink.framework.actors.ZLinkActorCreateResult
    permits systems.zlink.framework.actors.ZLinkActorCreateResult.Existing,
            systems.zlink.framework.actors.ZLinkActorCreateResult.Created,
            systems.zlink.framework.actors.ZLinkActorCreateResult.Rejected {
}
public interface systems.zlink.framework.actors.ZLinkActorRequestCall {
  public abstract systems.zlink.framework.actors.ZLinkActorRequestCall timeout(java.time.Duration);
  public abstract <TReply> java.util.concurrent.CompletionStage<TReply> submit(java.lang.Class<TReply>);
  public default <TReply> java.util.concurrent.CompletionStage<TReply> yield(java.lang.Class<TReply>);
}
public interface systems.zlink.framework.actors.ZLinkActorSendCall {
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> submit();
}
public interface systems.zlink.framework.actors.ZLinkBoundSession {
  public abstract systems.zlink.framework.actors.ZLinkBoundSessionSendCall send(java.lang.Object);
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> disconnect();
}
public interface systems.zlink.framework.actors.ZLinkBoundSessionSendCall {
  public abstract systems.zlink.framework.actors.ZLinkBoundSessionSendCall metadata(java.lang.String, java.lang.String);
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> submit();
}
```

ActorId는 UTF-8 1..255 bytes의 global logical ID다. `ActorRef`는 ActorId, positive signed-63-bit
ObjectGeneration과 조회 시점의 MeshName·NodeRid를 보존한다. 일반 message는 ActorId만 받고 current authority를
resolve한다. Destroy와 session bind만 exact ref를 받는다.

Create와 GetOrCreate call은 single-use다. 같은 option을 두 번 설정하면 `INVALID_CONFIGURATION`, submit을 두 번
호출하면 `ALREADY_SUBMITTED`다. `inMesh` 생략 시 object-role Mesh가 하나면 자동 선택하고 0개이면
`OBJECT_CLIENT_NOT_CONFIGURED`, 둘 이상이면 `MESH_SELECTION_REQUIRED`다. 명시한 Mesh가 없으면
`MESH_NOT_FOUND`다. Caller는 target RID나 placement callback을 지정하지 않는다. `find`와 `findSpot`은
current Ready ref만 반환하며 directory와 resolver를 제공하지 않는다.

`create`는 Ready Actor가 있으면 `ACTOR_ALREADY_EXISTS`이며 새 attempt에서는 `Created`
또는 `Rejected`를 반환한다. `getOrCreate`는 같은 type의 Ready Actor를 callback 없이
`Existing`으로 반환한다. Creating이면 authority 변경을 기다리며 CAS loser는
별도 factory나 callback을 시작하지 않는다. 서로 다른 operation은 Ready 뒤 `Existing`을
받고 cleanup 뒤 새 reservation을 경쟁하며 앞선 application reply를 공유하지 않는다.
같은 source Node RID·lifecycle generation·`OperationId`의 재전송만 correlation-free
`creation-operation-terminal-v1` envelope를 읽고 현재 correlation·reply route로 reply를
다시 encode한다. Terminal은 original deadline 뒤 5분 동안 유지한다. Callback exception은 `Rejected`가 아니라
typed creation failure다.

`ActorRef.objectGeneration()`은 `1..Long.MAX_VALUE`다. Typed JSON은 required property `actorId`,
`objectGeneration`, `meshName`, `nodeRid`를 사용하며 generation은 leading-zero 없는 decimal string으로 encode한다.
Unknown property는 무시하고 duplicate property, required property 누락, 숫자 token과 범위 밖 값은 거부한다.

Actor request에 선언된 `yield(...)`는 현재 Actor handler가 `SpotWide` User Spot의 shared execution
gate에서 실행 중일 때만 유효하다. Entry Spot Actor와 `PerActor` User Spot의 Actor가 호출하면 operation을
제출하거나 turn을 반환하지 않고 `INVALID_CONFIGURATION`으로 완료한다. Actor join은 `submit(...)`으로만
완료하며 `yield(...)`를 제공하지 않는다.
