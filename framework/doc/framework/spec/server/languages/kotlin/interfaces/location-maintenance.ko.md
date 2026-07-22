# Kotlin Location과 maintenance 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Java Location](../../java/interfaces/location-maintenance.ko.md) ·
[Location runtime](../../../40-location-runtime.ko.md) · [Location Store](../../../41-location-store-redis.ko.md)

Kotlin은 Java `ZLinkLocationStore`와 `ZLinkTransferStore`를 별도 capability로 사용한다. Location Store는
descriptor, authority, placement reservation과 canonical participant set을 소유하고 Transfer Store는 immutable
state·journal payload만 저장한다. Actor·Spot별 Store interface는 만들지 않는다. Kotlin의 두 suspending base class는
각 Java `CompletionStage` contract를 coroutine으로 연결할 뿐 key, version, generation, reservation, aggregate
fence와 transfer reference의 의미를 바꾸지 않는다.

Global authority key는 ActorId 또는 SpotRid를 기준으로 정한다. ActorId, SpotRid와 stable type은 UTF-8
1..255 bytes의 case-sensitive exact value다. Authority snapshot의 object generation과 owner generation은
provider가 발급하는 `1..Long.MAX_VALUE` 값이다. Descriptor와 operational snapshot은 node-wide placement
weight, active capacity, pending capacity와 현재 사용량을 typed field로 제공한다. Slot과 allocation group
field는 없다.
`ZLinkPlacementObjectKind`의 numeric value는 `ACTOR=1`, `USER_SPOT=2`, `INSTANCE_SPOT=3`이다. Kotlin은
ordinal을 저장하거나 전송하지 않고 `value()`를 사용한다.

## Kotlin source signature

```kotlin
abstract class ZLinkSuspendingLocationStore(
    scope: CoroutineScope = dispatcherScope(Dispatchers.IO),
    dispatcher: CoroutineDispatcher = Dispatchers.IO,
) : ZLinkLocationStore,
    ZLinkClientServerLocationStore,
    ZLinkFanoutLocationStore,
    ZLinkPeerLocationStore,
    ZLinkRouteLocationStore {
    protected fun <T> async(block: suspend () -> T): CompletionStage<T>

    protected abstract suspend fun readAuthoritySuspending(
        key: String,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkAuthorityReadResult

    protected abstract suspend fun compareExchangeAuthoritySuspending(
        key: ZLinkAuthorityKey,
        expectation: ZLinkAuthorityExpectation,
        mutation: ZLinkAuthorityMutation,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkAuthorityWriteResult

    protected abstract suspend fun listAuthoritiesSuspending(
        prefix: String,
        cursor: Optional<ZLinkAuthorityScanCursor>,
        limit: Int,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkAuthorityScanResult

    protected abstract suspend fun reserveSuspending(
        request: ZLinkObjectReservationRequest,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkObjectReserveResult

    protected abstract suspend fun commitSuspending(
        reservation: ZLinkObjectReservation,
        readyPayload: ByteArray,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkObjectCommitResult

    protected abstract suspend fun abortSuspending(
        reservation: ZLinkObjectReservation,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkObjectAbortResult

}

abstract class ZLinkSuspendingTransferStore(
    scope: CoroutineScope = dispatcherScope(Dispatchers.IO),
    dispatcher: CoroutineDispatcher = Dispatchers.IO,
) : ZLinkTransferStore {
    protected fun <T> async(block: suspend () -> T): CompletionStage<T>

    protected abstract suspend fun putSuspending(
        payload: ByteArray,
        retention: Duration,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkTransferStored

    protected abstract suspend fun getSuspending(
        reference: String,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkTransferReadResult

    protected abstract suspend fun renewSuspending(
        reference: String,
        retention: Duration,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkTransferRenewResult

    protected abstract suspend fun deleteSuspending(
        reference: String,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkTransferDeleteResult
}
```

`reserve`는 Actor, User Spot과 Instance Spot에 공통인 generic operation이다. Request는 object kind,
authority key, stable type, placement profile·affinity key, 최대 1 MiB인 creation intent reference·hash·encoded
size, target descriptor, exact owner token과 pending capacity delta를 가진다. `commit`과 `abort`는 reserve가 반환한 exact
reservation을 받으며 idempotent terminal result를 반환한다. Object별 `reserveActor`, `reserveSpot` 같은
interface는 제공하지 않는다.

Aggregate ID는 0이 아닌 128-bit 값이고 participant는 최대 1024개다. Encoded aggregate record는 최대
1 MiB다. Location Store의 participant list가 bounded canonical authority이며 prepare request의 32-byte
`inventoryDigest`는 participant별 mutation까지 포함한다. Transfer manifest는 payload lookup projection이고 두
digest가 일치할 때만 restore와 replay를 시작한다. Proposal, policy preflight, seal, capture, reservation prepare,
owner와 membership aggregate commit, restore·callback·ACK 순서는 Framework runtime이 조정한다. Location Store의
authority·membership aggregate commit만 하나의 transaction domain에 포함하며 Application과 provider adapter에는
이 순서를 조립하는 별도 public API가 없다.

Runtime은 immutable Transfer root를 먼저 저장하고 reference·checksum·retention과 manifest digest를 검증한 뒤
Location Store의 단일 CAS로 reference를 공개한다. CAS 전에 실패하거나 CAS conflict가 발생한 committed root는
orphan이며 고정 retention과 cleanup으로 제거한다. Root 교체는 새 root 저장과 검증, Location reference CAS,
이전 root cleanup 순서다. Transfer payload 사용을 끝낼 때는 Location Store에서 reference 사용 종료를 CAS한 뒤
Transfer Store에서 payload를 삭제한다. 두 Store 사이 transaction이나 2PC는 요구하지 않는다.

## Exact generated JVM signature

```java
public abstract class systems.zlink.framework.kotlin.ZLinkSuspendingLocationStore implements systems.zlink.framework.locations.ZLinkLocationStore, systems.zlink.framework.locations.ZLinkClientServerLocationStore, systems.zlink.framework.locations.ZLinkFanoutLocationStore, systems.zlink.framework.locations.ZLinkPeerLocationStore, systems.zlink.framework.locations.ZLinkRouteLocationStore {
  public systems.zlink.framework.kotlin.ZLinkSuspendingLocationStore(kotlinx.coroutines.CoroutineScope, kotlinx.coroutines.CoroutineDispatcher);
  protected abstract java.lang.Object readAuthoritySuspending(java.lang.String, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkAuthorityReadResult>);
  protected abstract java.lang.Object compareExchangeAuthoritySuspending(java.lang.String, systems.zlink.framework.locations.ZLinkAuthorityExpectation, systems.zlink.framework.locations.ZLinkAuthorityMutation, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkAuthorityWriteResult>);
  protected abstract java.lang.Object listAuthoritiesSuspending(java.lang.String, java.util.Optional<systems.zlink.framework.locations.ZLinkAuthorityScanCursor>, int, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkAuthorityScanResult>);
  protected abstract java.lang.Object reserveSuspending(systems.zlink.framework.locations.ZLinkObjectReservationRequest, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkObjectReserveResult>);
  protected abstract java.lang.Object commitSuspending(systems.zlink.framework.locations.ZLinkObjectReservation, byte[], systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkObjectCommitResult>);
  protected abstract java.lang.Object abortSuspending(systems.zlink.framework.locations.ZLinkObjectReservation, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkObjectAbortResult>);
}
public abstract class systems.zlink.framework.kotlin.ZLinkSuspendingTransferStore implements systems.zlink.framework.locations.ZLinkTransferStore {
  public systems.zlink.framework.kotlin.ZLinkSuspendingTransferStore(kotlinx.coroutines.CoroutineScope, kotlinx.coroutines.CoroutineDispatcher);
  protected abstract java.lang.Object putSuspending(byte[], java.time.Duration, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkTransferStored>);
  protected abstract java.lang.Object getSuspending(java.lang.String, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkTransferReadResult>);
  protected abstract java.lang.Object renewSuspending(java.lang.String, java.time.Duration, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkTransferRenewResult>);
  protected abstract java.lang.Object deleteSuspending(java.lang.String, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkTransferDeleteResult>);
}
```

Provider에 전달한 `ByteArray`는 `CompletionStage` 완료까지 immutable하다. Provider가 그 뒤 보관하려면 완료
전에 복사한다. Cursor는 opaque하고 first page는 empty `Optional`을 사용한다. Kotlin은 Spot handle resolver,
Actor directory, slot acquire/release/provider와 unbounded object list extension을 제공하지 않는다. 운영 조회는
Java의 bounded page operation을 직접 사용한다. Route miss는 negative cache에 저장하지 않는다.

공식 Redis extension도 Java의 `ZLinkRedisLocationStore`와 `ZLinkRedisTransferStore`를 별도 instance로 사용한다.
같은 Redis deployment를 서로 다른 prefix로 사용할 수 있지만 한 instance가 두 capability를 함께 구현하지 않는다.
Published reference의 permanent missing 또는 checksum·inventory digest mismatch는 `TransferDataLost`이며 이전 owner로
rollback하지 않는다.
