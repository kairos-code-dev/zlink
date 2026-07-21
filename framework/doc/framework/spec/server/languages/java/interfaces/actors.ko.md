# Java Actor 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Actor 공통 계약](../../../22-actor-model.ko.md)

```java
public interface ZLinkActorFactory {
    CompletionStage<ZLinkActor> create(String actorId, ZLinkActorContext context);
}

public interface ZLinkActorHandlerRegistry {
    void addHandler(Class<?> handlerType);
}

public interface ZLinkTransferCancellation {
    boolean isCancellationRequested();
}

public interface ZLinkTransferStateAdapter<TInstance, TState> {
    CompletionStage<TState> capture(
        TInstance instance, ZLinkTransferCancellation cancellation);
    CompletionStage<Void> restore(
        TInstance instance, TState state, ZLinkTransferCancellation cancellation);
}

public sealed interface ZLinkTransferPolicy<TInstance>
    permits ZLinkTransferPolicy.Disabled,
            ZLinkTransferPolicy.Recreate,
            ZLinkTransferPolicy.Snapshot {
    record Disabled<T>() implements ZLinkTransferPolicy<T> {}
    record Recreate<T>() implements ZLinkTransferPolicy<T> {}
    record Snapshot<T, TState>(
        String stateContractId,
        Class<TState> stateClass,
        Class<? extends ZLinkTransferStateAdapter<T, TState>> adapterClass)
        implements ZLinkTransferPolicy<T> {}

    static <T> ZLinkTransferPolicy<T> disabled() {
        return new Disabled<>();
    }
    static <T> ZLinkTransferPolicy<T> recreate() {
        return new Recreate<>();
    }
    static <T, TState> ZLinkTransferPolicy<T> snapshot(
        String stateContractId,
        Class<TState> stateClass,
        Class<? extends ZLinkTransferStateAdapter<T, TState>> adapterClass) {
        return new Snapshot<>(stateContractId, stateClass, adapterClass);
    }
}

```

Factory registration의 정확한 builder member는
[구성과 host](configuration-host.ko.md)가 소유한다. Stateful maintenance policy는 Actor factory 등록에
직접 연결한다. Runtime은 factory가 반환한 Actor를 명시한 `actorClass`로 검사해 type 불일치를 startup
오류로 반환한다. Factory와 분리된 transfer registry는 제공하지 않는다.
`Snapshot` adapter는 factory가 만든 instance의 typed state만 capture·restore하며 owner claim, checkpoint
envelope, generation과 recovery phase를 받지 않는다.

같은 `stateContractId`를 사용하는 source와 target adapter는 `frameworkJsonV1` semantic profile로 호환되어야
한다. 이 profile은 enum을 string, 64-bit integer를 decimal string, binary를 padded base64로 표현하고 unknown
field는 무시한다. Duplicate field와 required field 누락은 거부한다. Application state의 JSON byte 배열 자체는
canonical하지 않으며 Checkpoint Store에는 opaque bytes로 보관한다. Canonical byte identity는 Framework 내부
root manifest, chunk와 envelope에만 적용한다. Message별 codec 등록이나 transfer 전용 codec API는 제공하지
않는다.

Target이 `Activated`에 도달해도 application과 session ingress는 sealed 상태를 유지하고 restore, accepted
journal replay와 bound-session route는 staged 상태로만 준비한다. Source cleanup이 terminal 상태에 도달하고
authority의 `COMPLETED` CAS가 성공한 뒤에만 target을 `READY`로 열고 checkpoint fence를 해제한다. `COMPLETED`
뒤의 target failure는 ordinary owner loss로 처리하며 이전 checkpoint를 transparent replay하지 않는다. 이
barrier를 조작하는 public phase API는 제공하지 않는다.

Target replacement가 발생하면 stable transfer 안의 각 attempt가 factory와 `restore(...)`를 at-least-once
호출할 수 있고 중단된 stale attempt callback이 successor와 겹칠 수 있다. `capture(...)`도 immutable checkpoint
root가 authority에 연결되기 전까지 반복될 수 있다. Current exact owner와 attempt fence만 completion을 commit하고
admission을 열 수 있다. Callback에는 transfer ID를 추가하지 않으므로 application restore와 capture는 retry-safe해야
하며 exactly-once external side effect를 보장하지 않는다.

Transferred terminal reply accounting은 internal command ID 46 `replyRelayAck`를 사용한다. 이 command는 stable
transfer ID, operation ID, exact request-source fence(owner ID, lease generation, node RID, node generation)와
status만 가지며 payload와 metadata를 싣지 않는다. Physical connection close는 terminal 증거가 아니다. ACK 또는
accepted record에 저장한 exact request-source lease expiry만 terminal accounting을 완료하며 public ACK API는 없다.

Source는 connection-bound one-way를 포함해 admission한 모든 connection-bound work가 terminal accounting에
도달한 뒤에만 `CAPTURED`를 commit한다. Durable accepted journal은 exact owner lease가 있는 source에서만
사용한다. Pre-`CAPTURED` drain이 deadline 안에 끝나지 않으면 transfer를 abort하고 host Retire를
`BLOCKED/TRANSFER_DISABLED`로 끝낸다. Connection-bound one-way를 미완료 상태로 capture하는 예외는 없다.

Transferable Actor는 source Entry Spot member여야 한다. User Spot member가 하나라도 남아 있으면 Retire
preflight는 `BLOCKED/TRANSFER_DISABLED`이고 source authority와 admission을 바꾸지 않는다. `NEW_OWNER` CAS는
owner, authority owner generation과 current Spot을 target Entry identity로 원자적으로 바꾼다. Target factory와
restore, target `onJoinedActor`, journal replay 뒤에 source `onLeaveActor`와 old Entry membership 제거를
durable cleanup으로 수행한다. Lifecycle callback은 retry-safe해야 하며 at-least-once 호출될 수 있다. 이
순서를 제어하는 public phase API는 없다.

새 distributed Actor는 authority 내부 `CREATING` row를 `NEW_OBJECT` CAS로 만들고 최종
`ActorRef.generation()`, factory 실행, initial Entry membership과 initialize를 완료한 뒤 `READY` CAS를
수행한다. Resolver와 remote messaging은 `READY`만 사용한다. Factory나 initialize가 실패하면 exact owner
fence로 delete하고 결과를 read해 reconcile한다. Delete가 확인될 때까지 같은 typed failure를 반환하고 hidden
retry는 0이며, `MISSING`이 확인된 뒤 다음 caller만 새 `CREATING`을 시작한다. Entry Spot initialization도 Host
`SERVING` publication보다 먼저 완료한다. 이 barrier를 위한 public API는 없다.

## Exact public member inventory

아래 선언은 이 category의 Java public type과 member를 고정한다.

```java
public interface systems.zlink.framework.actors.ZLinkActorFactory {
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.actors.ZLinkActor> create(java.lang.String, systems.zlink.framework.actors.ZLinkActorContext);
}
public interface systems.zlink.framework.actors.ZLinkTransferCancellation {
  public abstract boolean isCancellationRequested();
}
public interface systems.zlink.framework.actors.ZLinkTransferStateAdapter<TInstance, TState> {
  public abstract java.util.concurrent.CompletionStage<TState> capture(TInstance, systems.zlink.framework.actors.ZLinkTransferCancellation);
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> restore(TInstance, TState, systems.zlink.framework.actors.ZLinkTransferCancellation);
}
public sealed interface systems.zlink.framework.actors.ZLinkTransferPolicy<TInstance>
    permits systems.zlink.framework.actors.ZLinkTransferPolicy.Disabled,
            systems.zlink.framework.actors.ZLinkTransferPolicy.Recreate,
            systems.zlink.framework.actors.ZLinkTransferPolicy.Snapshot {
  public static <T> systems.zlink.framework.actors.ZLinkTransferPolicy<T> disabled();
  public static <T> systems.zlink.framework.actors.ZLinkTransferPolicy<T> recreate();
  public static <T, TState> systems.zlink.framework.actors.ZLinkTransferPolicy<T> snapshot(java.lang.String, java.lang.Class<TState>, java.lang.Class<? extends systems.zlink.framework.actors.ZLinkTransferStateAdapter<T, TState>>);
}
public final class systems.zlink.framework.actors.ZLinkTransferPolicy$Disabled<T> extends java.lang.Record implements systems.zlink.framework.actors.ZLinkTransferPolicy<T> {
  public systems.zlink.framework.actors.ZLinkTransferPolicy$Disabled();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
}
public final class systems.zlink.framework.actors.ZLinkTransferPolicy$Recreate<T> extends java.lang.Record implements systems.zlink.framework.actors.ZLinkTransferPolicy<T> {
  public systems.zlink.framework.actors.ZLinkTransferPolicy$Recreate();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
}
public final class systems.zlink.framework.actors.ZLinkTransferPolicy$Snapshot<T, TState> extends java.lang.Record implements systems.zlink.framework.actors.ZLinkTransferPolicy<T> {
  public systems.zlink.framework.actors.ZLinkTransferPolicy$Snapshot(java.lang.String, java.lang.Class<TState>, java.lang.Class<? extends systems.zlink.framework.actors.ZLinkTransferStateAdapter<T, TState>>);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String stateContractId();
  public java.lang.Class<TState> stateClass();
  public java.lang.Class<? extends systems.zlink.framework.actors.ZLinkTransferStateAdapter<T, TState>> adapterClass();
}
public interface systems.zlink.framework.actors.ZLinkActorHandlerRegistry {
  public abstract void addHandler(java.lang.Class<?>);
}
public final class systems.zlink.framework.actors.ActorRef extends java.lang.Record {
  public systems.zlink.framework.actors.ActorRef(systems.zlink.contracts.core.RoutingId, java.lang.String, long);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.contracts.core.RoutingId nodeRid();
  public java.lang.String actorId();
  public long generation();
}
public final class systems.zlink.framework.actors.ActorRefSnapshot extends java.lang.Record {
  public systems.zlink.framework.actors.ActorRefSnapshot(systems.zlink.contracts.core.RoutingId, java.lang.String, long);
  public static systems.zlink.framework.actors.ActorRefSnapshot from(systems.zlink.framework.actors.ActorRef);
  public systems.zlink.framework.actors.ActorRef toActorRef();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.contracts.core.RoutingId nodeRid();
  public java.lang.String actorId();
  public long generation();
}
public interface systems.zlink.framework.actors.ZLinkActor {
  public abstract java.lang.String actorId();
  public abstract systems.zlink.framework.actors.ZLinkActorContext context();
  public default void configure();
}
public interface systems.zlink.framework.actors.ZLinkActorClient {
  public abstract systems.zlink.framework.actors.ZLinkActorSendCall sendToActor(java.lang.String, systems.zlink.framework.actors.ActorRef, java.lang.Object);
  public abstract systems.zlink.framework.actors.ZLinkActorRequestCall requestToActor(java.lang.String, systems.zlink.framework.actors.ActorRef, java.lang.Object);
}
public interface systems.zlink.framework.actors.ZLinkActorContext {
  public abstract java.lang.String meshName();
  public abstract java.util.Optional<systems.zlink.contracts.core.RoutingId> spotRid();
  public abstract systems.zlink.framework.actors.ZLinkBoundSession boundSession();
  public abstract systems.zlink.framework.actors.ZLinkActorJoinCall joinSpot(systems.zlink.contracts.core.RoutingId, java.lang.Object);
  public abstract systems.zlink.framework.actors.ZLinkActorJoinCall joinEntrySpot(systems.zlink.contracts.core.RoutingId, java.lang.Object);
}
public interface systems.zlink.framework.actors.ZLinkActorDirectory {
  public abstract java.util.concurrent.CompletionStage<java.util.Optional<systems.zlink.framework.actors.ActorRef>> find(java.lang.String, java.lang.String);
}
public interface systems.zlink.framework.actors.ZLinkActorJoinCall {
  public abstract systems.zlink.framework.actors.ZLinkActorJoinCall timeout(java.time.Duration);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.actors.ZLinkActorJoinResult<java.lang.Void>> submit();
  public default java.util.concurrent.CompletionStage<systems.zlink.framework.actors.ZLinkActorJoinResult<java.lang.Void>> yield();
  public abstract <TReply> java.util.concurrent.CompletionStage<systems.zlink.framework.actors.ZLinkActorJoinResult<TReply>> submit(java.lang.Class<TReply>);
  public default <TReply> java.util.concurrent.CompletionStage<systems.zlink.framework.actors.ZLinkActorJoinResult<TReply>> yield(java.lang.Class<TReply>);
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
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.actors.ActorRef> create(java.lang.String, java.lang.String, java.lang.String);
  public default java.util.concurrent.CompletionStage<systems.zlink.framework.actors.ActorRef> create(java.lang.String, java.lang.String, java.lang.String, java.lang.Object);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.actors.ActorRef> create(java.lang.String, java.lang.String, java.lang.String, systems.zlink.framework.messaging.ZLinkMessage);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.actors.ActorRef> getOrCreate(java.lang.String, java.lang.String, java.lang.String);
  public default java.util.concurrent.CompletionStage<systems.zlink.framework.actors.ActorRef> getOrCreate(java.lang.String, java.lang.String, java.lang.String, java.lang.Object);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.actors.ActorRef> getOrCreate(java.lang.String, java.lang.String, java.lang.String, systems.zlink.framework.messaging.ZLinkMessage);
}
public interface systems.zlink.framework.actors.ZLinkActorRequestCall {
  public abstract systems.zlink.framework.actors.ZLinkActorRequestCall timeout(java.time.Duration);
  public abstract <TReply> java.util.concurrent.CompletionStage<TReply> submit(java.lang.Class<TReply>);
  public default <TReply> java.util.concurrent.CompletionStage<TReply> yield(java.lang.Class<TReply>);
}
public interface systems.zlink.framework.actors.ZLinkActorSendCall {
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> submit();
}
public interface systems.zlink.framework.actors.ZLinkBoundSession {
  public abstract systems.zlink.framework.actors.ZLinkBoundSessionSendCall send(java.lang.Object);
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> disconnect();
}
public interface systems.zlink.framework.actors.ZLinkBoundSessionSendCall {
  public abstract systems.zlink.framework.actors.ZLinkBoundSessionSendCall metadata(java.lang.String, java.lang.String);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> submit();
}
```

Canonical logical identity는 `(MeshName, ActorId)`다. Actor type은 create에서 factory를 선택한 뒤 authority
payload에 고정하는 immutable lifecycle attribute이며 `ActorRef`나 directory key에 반복하지 않는다. 같은
MeshName과 Actor ID에는 active type 하나만 존재한다. `getOrCreate`에 전달한 type이 existing authority의 type과
다르면 type conflict로 실패한다.

`ZLinkActorDirectory`는 MeshName과 Actor ID로 이미 존재하는 logical Actor만 조회한다. Missing Actor를
생성하거나 remote MeshNode를 선택하지 않는다. Local create와 get-or-create는 `ZLinkActorManager`가 actor
type을 명시해서 수행한다. MeshName은 현재 host에 등록된 local MeshNode를 선택한다. Existing Actor가 remote
owner에 있으면 조회 결과를 반환할 수 있지만, missing Actor를 remote owner에 생성하거나 hidden forwarding으로
만들지 않는다.
