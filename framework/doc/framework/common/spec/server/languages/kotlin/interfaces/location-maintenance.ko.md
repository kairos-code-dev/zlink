# Kotlin Location과 maintenance 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Java Location](../../java/interfaces/location-maintenance.ko.md) ·
[Location runtime](../../../../40-location-runtime.ko.md) · [Location Store](../../../../41-location-store-redis.ko.md)

Kotlin은 Java `ZLinkLocationStore`와 `ZLinkRelocationStore`를 별도 capability로 사용한다. Location Store는
descriptor, authority, placement reservation과 canonical participant set을 소유하고 Relocation Store는 immutable
state·journal payload만 저장한다. Actor·Spot별 Store interface는 만들지 않는다. Kotlin의 두 suspending base class는
각 Java `CompletionStage` contract를 coroutine으로 연결할 뿐 key, version, generation, reservation, aggregate
fence와 relocation reference의 의미를 바꾸지 않는다.

Kotlin configuration은 Java `ZLinkLocationOptions`의 relocation 제한을 그대로 사용한다. 기본값은 active outbound
64, active inbound 64, concurrent Capture 8, concurrent Restore 8, encoded payload in flight 268,435,456 bytes다.
다섯 값은 모두 양수이고 같은 process의 모든 MeshNode가 공유한다. 모든 permit을 얻기 전에는 source queue를
seal하지 않으며 byte 한도를 넘는 단일 User Spot aggregate는 다른 payload 단계와 겹치지 않게 단독 실행한다.
Byte reservation은 Snapshot participant마다 64 MiB와 Framework-owned section의 deterministic encoded upper
bound를 합하며 `Capture` 뒤 actual encoded size로만 축소한다.
다섯 getter와 다섯 setter는 Java exact public member inventory의 JVM method를 그대로 호출한다. Kotlin 전용
property, relocation limit wrapper와 `$default` member는 생성하지 않는다.

Global [authority](../../../../01-glossary.ko.md#authority) key는 ActorId 또는 SpotId를 기준으로 정한다. ActorId, SpotId와 stable type은 UTF-8
1..255 bytes의 case-sensitive exact value다. Authority [snapshot](../../../../01-glossary.ko.md#snapshot)의 object generation과 owner generation은
provider가 발급하는 `1..Long.MAX_VALUE` 값이다. Snapshot과 stored result의
`ZLinkPlacementAllocation`은 Pending 또는 Active 상태, object kind, [stable type](../../../../01-glossary.ko.md#stable-type), [descriptor](../../../../01-glossary.ko.md#descriptor) key·lifecycle
generation과 `1..Integer.MAX_VALUE` capacity delta를 포함한다. Generic reserve만 Missing에서 Pending을
만들고 commit만 Pending을 Active로 전환한다. Generic abort는 exact reservation을 기준으로 current target
liveness와 관계없이 Pending을 정리한다. Existing Active authority의 preserve·[owner](../../../../01-glossary.ko.md#owner) relocation·delete만
`ZLinkAuthorityExpectFound`를 사용한다. Descriptor와 operational snapshot은 node-wide placement
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
    private fun <T> async(block: suspend () -> T): CompletionStage<T>

    final override fun read(
        key: String,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkAuthorityReadResult> =
        async { readAuthoritySuspending(key, cancellation) }

    final override fun compareExchange(
        key: String,
        expectation: ZLinkAuthorityExpectation,
        mutation: ZLinkAuthorityMutation,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkAuthorityWriteResult> =
        async { compareExchangeAuthoritySuspending(key, expectation, mutation, cancellation) }

    final override fun list(
        prefix: String,
        cursor: Optional<ZLinkAuthorityScanCursor>,
        limit: Int,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkAuthorityScanResult> =
        async { listAuthoritiesSuspending(prefix, cursor, limit, cancellation) }

    final override fun reserve(
        request: ZLinkObjectReservationRequest,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkObjectReserveResult> =
        async { reserveSuspending(request, cancellation) }

    final override fun commit(
        reservation: ZLinkObjectReservation,
        readyPayload: ByteArray,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkObjectCommitResult> =
        async { commitSuspending(reservation, readyPayload, cancellation) }

    final override fun completeCreation(
        reservation: ZLinkObjectReservation,
        completion: ZLinkObjectCreationCompletion,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkObjectCreationCompleteResult> =
        async { completeCreationSuspending(reservation, completion, cancellation) }

    final override fun readCreationTerminal(
        operation: ZLinkCreationOperationIdentity,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkCreationTerminalReadResult> =
        async { readCreationTerminalSuspending(operation, cancellation) }

    final override fun abort(
        reservation: ZLinkObjectReservation,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkObjectAbortResult> =
        async { abortSuspending(reservation, cancellation) }

    final override fun reserveRelocationCapacity(
        request: ZLinkRelocationCapacityReservationRequest,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkRelocationCapacityReserveResult> =
        async { reserveRelocationCapacitySuspending(request, cancellation) }

    final override fun abortRelocationCapacity(
        fence: ZLinkRelocationCapacityFence,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkRelocationCapacityAbortResult> =
        async { abortRelocationCapacitySuspending(fence, cancellation) }

    final override fun prepareAggregate(
        request: ZLinkAggregatePrepareRequest,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkAggregatePrepareResult> =
        async { prepareAggregateSuspending(request, cancellation) }

    final override fun commitAggregate(
        fence: ZLinkAggregateFence,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkAggregateCommitResult> =
        async { commitAggregateSuspending(fence, cancellation) }

    final override fun abortAggregate(
        fence: ZLinkAggregateFence,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkAggregateAbortResult> =
        async { abortAggregateSuspending(fence, cancellation) }

    final override fun updateMeshNode(
        descriptor: ZLinkMeshNodeDescriptor,
        intent: ZLinkLocationWriteIntent,
    ): CompletionStage<ZLinkLocationWriteResult> =
        async { updateMeshNodeSuspending(descriptor, intent) }

    final override fun removeMeshNode(
        key: ZLinkMeshNodeDescriptorKey,
        owner: ZLinkLocationOwnerToken,
    ): CompletionStage<ZLinkLocationWriteStatus> =
        async { removeMeshNodeSuspending(key, owner) }

    final override fun listMeshNodes(
        meshName: String,
        page: ZLinkPageRequest,
    ): CompletionStage<ZLinkLocationPage<ZLinkMeshNodeDescriptor>> =
        async { listMeshNodesSuspending(meshName, page) }

    final override fun claimOwnerLease(
        ownerId: String,
        leaseTtl: Duration,
    ): CompletionStage<ZLinkOwnerLeaseClaimResult> =
        async { claimOwnerLeaseSuspending(ownerId, leaseTtl) }

    final override fun readOwnerLease(
        ownerId: String,
    ): CompletionStage<ZLinkOwnerLeaseReadResult> =
        async { readOwnerLeaseSuspending(ownerId) }

    final override fun renewOwnerLease(
        token: ZLinkLocationOwnerToken,
        leaseTtl: Duration,
    ): CompletionStage<ZLinkOwnerLeaseRenewResult> =
        async { renewOwnerLeaseSuspending(token, leaseTtl) }

    final override fun releaseOwnerLease(
        token: ZLinkLocationOwnerToken,
    ): CompletionStage<ZLinkOwnerLeaseReleaseResult> =
        async { releaseOwnerLeaseSuspending(token) }

    final override fun removeAllByOwner(
        owner: ZLinkLocationOwnerToken,
    ): CompletionStage<Long> =
        async { removeAllByOwnerSuspending(owner) }

    final override fun updateClientServer(
        descriptor: ZLinkClientServerServerDescriptor,
        intent: ZLinkLocationWriteIntent,
    ): CompletionStage<ZLinkLocationWriteResult> =
        async { updateClientServerSuspending(descriptor, intent) }

    final override fun removeClientServer(
        key: ZLinkClientServerServerDescriptorKey,
        owner: ZLinkLocationOwnerToken,
    ): CompletionStage<ZLinkLocationWriteStatus> =
        async { removeClientServerSuspending(key, owner) }

    final override fun listClientServers(
        channelName: String,
        page: ZLinkPageRequest,
    ): CompletionStage<ZLinkLocationPage<ZLinkClientServerServerDescriptor>> =
        async { listClientServersSuspending(channelName, page) }

    final override fun updateFanoutPublisher(
        descriptor: ZLinkFanoutPublisherDescriptor,
        intent: ZLinkLocationWriteIntent,
    ): CompletionStage<ZLinkLocationWriteResult> =
        async { updateFanoutPublisherSuspending(descriptor, intent) }

    final override fun removeFanoutPublisher(
        key: ZLinkFanoutPublisherDescriptorKey,
        owner: ZLinkLocationOwnerToken,
    ): CompletionStage<ZLinkLocationWriteStatus> =
        async { removeFanoutPublisherSuspending(key, owner) }

    final override fun listFanoutPublishers(
        channelName: String,
        page: ZLinkPageRequest,
    ): CompletionStage<ZLinkLocationPage<ZLinkFanoutPublisherDescriptor>> =
        async { listFanoutPublishersSuspending(channelName, page) }

    final override fun updatePeer(
        peer: ZLinkPeerLocation,
        intent: ZLinkLocationWriteIntent,
    ): CompletionStage<ZLinkLocationWriteResult> =
        async { updatePeerSuspending(peer, intent) }

    final override fun removePeer(
        key: ZLinkPeerLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): CompletionStage<ZLinkLocationWriteResult> =
        async { removePeerSuspending(key, owner) }

    final override fun listPeerLocations(
        filter: ZLinkPeerLocationFilter,
        page: ZLinkPageRequest,
    ): CompletionStage<ZLinkLocationPage<ZLinkPeerLocation>> =
        async { listPeerLocationsSuspending(filter, page) }

    final override fun updateRoute(
        route: ZLinkRouteLocation,
        intent: ZLinkLocationWriteIntent,
    ): CompletionStage<ZLinkLocationWriteResult> =
        async { updateRouteSuspending(route, intent) }

    final override fun removeRoute(
        key: ZLinkRouteLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): CompletionStage<ZLinkLocationWriteResult> =
        async { removeRouteSuspending(key, owner) }

    final override fun resolveRoute(
        key: ZLinkRouteLocationKey,
    ): CompletionStage<ZLinkRouteLocation?> =
        async { resolveRouteSuspending(key) }

    final override fun listRouteLocations(
        filter: ZLinkRouteLocationFilter,
        page: ZLinkPageRequest,
    ): CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> =
        async { listRouteLocationsSuspending(filter, page) }

    protected abstract suspend fun readAuthoritySuspending(
        key: String,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkAuthorityReadResult

    protected abstract suspend fun compareExchangeAuthoritySuspending(
        key: String,
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

    protected abstract suspend fun completeCreationSuspending(
        reservation: ZLinkObjectReservation,
        completion: ZLinkObjectCreationCompletion,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkObjectCreationCompleteResult

    protected abstract suspend fun readCreationTerminalSuspending(
        operation: ZLinkCreationOperationIdentity,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkCreationTerminalReadResult

    protected abstract suspend fun abortSuspending(
        reservation: ZLinkObjectReservation,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkObjectAbortResult

    protected abstract suspend fun reserveRelocationCapacitySuspending(
        request: ZLinkRelocationCapacityReservationRequest,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkRelocationCapacityReserveResult

    protected abstract suspend fun abortRelocationCapacitySuspending(
        fence: ZLinkRelocationCapacityFence,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkRelocationCapacityAbortResult

    protected abstract suspend fun prepareAggregateSuspending(
        request: ZLinkAggregatePrepareRequest,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkAggregatePrepareResult

    protected abstract suspend fun commitAggregateSuspending(
        fence: ZLinkAggregateFence,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkAggregateCommitResult

    protected abstract suspend fun abortAggregateSuspending(
        fence: ZLinkAggregateFence,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkAggregateAbortResult

    protected abstract suspend fun updateMeshNodeSuspending(
        descriptor: ZLinkMeshNodeDescriptor,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun removeMeshNodeSuspending(
        key: ZLinkMeshNodeDescriptorKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteStatus

    protected abstract suspend fun listMeshNodesSuspending(
        meshName: String,
        page: ZLinkPageRequest,
    ): ZLinkLocationPage<ZLinkMeshNodeDescriptor>

    protected abstract suspend fun claimOwnerLeaseSuspending(
        ownerId: String,
        leaseTtl: Duration,
    ): ZLinkOwnerLeaseClaimResult

    protected abstract suspend fun readOwnerLeaseSuspending(
        ownerId: String,
    ): ZLinkOwnerLeaseReadResult

    protected abstract suspend fun renewOwnerLeaseSuspending(
        token: ZLinkLocationOwnerToken,
        leaseTtl: Duration,
    ): ZLinkOwnerLeaseRenewResult

    protected abstract suspend fun releaseOwnerLeaseSuspending(
        token: ZLinkLocationOwnerToken,
    ): ZLinkOwnerLeaseReleaseResult

    protected abstract suspend fun removeAllByOwnerSuspending(
        owner: ZLinkLocationOwnerToken,
    ): Long

    protected abstract suspend fun updateClientServerSuspending(
        descriptor: ZLinkClientServerServerDescriptor,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun removeClientServerSuspending(
        key: ZLinkClientServerServerDescriptorKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteStatus

    protected abstract suspend fun listClientServersSuspending(
        channelName: String,
        page: ZLinkPageRequest,
    ): ZLinkLocationPage<ZLinkClientServerServerDescriptor>

    protected abstract suspend fun updateFanoutPublisherSuspending(
        descriptor: ZLinkFanoutPublisherDescriptor,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun removeFanoutPublisherSuspending(
        key: ZLinkFanoutPublisherDescriptorKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteStatus

    protected abstract suspend fun listFanoutPublishersSuspending(
        channelName: String,
        page: ZLinkPageRequest,
    ): ZLinkLocationPage<ZLinkFanoutPublisherDescriptor>

    protected abstract suspend fun updatePeerSuspending(
        peer: ZLinkPeerLocation,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun removePeerSuspending(
        key: ZLinkPeerLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun listPeerLocationsSuspending(
        filter: ZLinkPeerLocationFilter,
        page: ZLinkPageRequest,
    ): ZLinkLocationPage<ZLinkPeerLocation>

    protected abstract suspend fun updateRouteSuspending(
        route: ZLinkRouteLocation,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun removeRouteSuspending(
        key: ZLinkRouteLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun resolveRouteSuspending(
        key: ZLinkRouteLocationKey,
    ): ZLinkRouteLocation?

    protected abstract suspend fun listRouteLocationsSuspending(
        filter: ZLinkRouteLocationFilter,
        page: ZLinkPageRequest,
    ): ZLinkLocationPage<ZLinkRouteLocation>
}

abstract class ZLinkSuspendingRelocationStore(
    scope: CoroutineScope = dispatcherScope(Dispatchers.IO),
    dispatcher: CoroutineDispatcher = Dispatchers.IO,
) : ZLinkRelocationStore {
    private fun <T> async(block: suspend () -> T): CompletionStage<T>

    final override fun put(
        payload: ByteArray,
        retention: Duration,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkRelocationStored> =
        async { putSuspending(payload, retention, cancellation) }

    final override fun get(
        reference: String,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkRelocationReadResult> =
        async { getSuspending(reference, cancellation) }

    final override fun renew(
        reference: String,
        retention: Duration,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkRelocationRenewResult> =
        async { renewSuspending(reference, retention, cancellation) }

    final override fun delete(
        reference: String,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkRelocationDeleteResult> =
        async { deleteSuspending(reference, cancellation) }

    protected abstract suspend fun putSuspending(
        payload: ByteArray,
        retention: Duration,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkRelocationStored

    protected abstract suspend fun getSuspending(
        reference: String,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkRelocationReadResult

    protected abstract suspend fun renewSuspending(
        reference: String,
        retention: Duration,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkRelocationRenewResult

    protected abstract suspend fun deleteSuspending(
        reference: String,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkRelocationDeleteResult
}
```

두 primary constructor의 인자는 모두 기본값이 있으므로 JVM에는 public no-arg constructor와
`CoroutineScope`, `CoroutineDispatcher`를 받는 public constructor가 생성된다. Compiler가 기본 인자를
처리하기 위해 만드는 synthetic mask constructor는 public contract에 포함하지 않는다. Java Store method는
subclass가 다시 override할 수 없는 `public final` bridge이며, provider 구현은 protected suspend hook만 구현한다.

`reserve`는 Actor, User Spot과 Instance Spot에 공통인 generic operation이다. Request는 object kind,
authority key, stable type, 최대 1 MiB인 creation intent reference·hash·encoded
size, target descriptor, exact owner token과 pending capacity delta를 가진다. `commit`과 `abort`는 reserve가 반환한 exact
reservation을 받으며 idempotent terminal result를 반환한다. Object별 `reserveActor`, `reserveSpot` 같은
interface는 제공하지 않는다.

Actor manager 생성은 `completeCreation`으로 Created·Rejected·Failed를 기록하고 infrastructure recovery만
`abort`로 reservation을 정리한다. 서로 다른 operation이 같은 Creating authority를 관찰하면 authority 종료를
기다린다. Ready로 끝나면 각 요청에 Existing을 반환하고, Rejected·Failed 정리가 끝났으면 각 요청이 새
reservation을 얻어 실행한다. 이전 operation의 application reply를 공유하지 않는다. `readCreationTerminal`은
source node RID raw bytes, source lifecycle generation과 128-bit operation ID가 모두 같은 replay에만
`creation-operation-terminal-v1` semantic envelope를 반환한다. Envelope에는 transport correlation과 reply
route가 없으며 runtime은 현재 요청의 framing을 새로 만든다. Terminal은 원래 요청 deadline에서 최소 5분 유지한다.

Java `ZLinkAuthoritySnapshot.pendingCreation()`은 Pending allocation에서 반드시 non-empty이고 Active에서는
empty다. 값은 provider-issued reservation ID와 Actor·User Spot·[Instance Spot](../../../../01-glossary.ko.md#entry-spot-user-spot과-instance-spot) 생성 요청의 immutable content
reference, exact 32-byte SHA-256과 `0..1 MiB` encoded size를 반환한다. Target-owned Instance Spot의 cold
activation content만 complete `instance-activation-recovery-v1` envelope이며, Actor와 User Spot의 manager
create content에는 이 envelope를 사용하지 않는다. Kotlin provider와 runtime은 별도 process-local reservation
index를 만들지 않고 이 projection으로 exact Commit 또는 Abort fence를 복원한다.

Aggregate ID는 0이 아닌 128-bit 값이고 participant는 최대 1024개다. Encoded aggregate record는 최대
1 MiB다. [Location Store](../../../../01-glossary.ko.md#location-store)의 participant list가 bounded canonical authority이며 prepare request의 32-byte
`inventoryDigest`는 participant별 mutation까지 포함한다. Relocation manifest는 payload lookup projection이고 두
digest가 일치할 때만 restore와 replay를 시작한다. Proposal, policy preflight, seal, capture, reservation prepare,
restore, owner와 membership aggregate commit, callback·replay·ACK 순서는 Framework runtime이 조정한다. Location Store의
authority·[membership](../../../../01-glossary.ko.md#membership) aggregate commit만 하나의 transaction domain에 포함하며 Application과 provider adapter에는
이 순서를 조립하는 별도 public API가 없다.

상속한 `prepareAggregate(...)`는 participant의 `ownerTransition`으로 두 mode를 판정한다. `NEW_OWNER`가 하나라도
있는 relocation mode는 `PRESERVE` participant와 섞을 수 있지만 non-zero capacity bundle은 `NEW_OWNER`
participant의 durable allocation delta만 exact 합산한다. 모든 participant가 `PRESERVE`인 completion·steady-
normalization mode는 exact zero capacity와 모든 empty membership mutation을 요구한다. 이 mode는 capacity를
예약하거나 바꾸지 않고 participant payload만 atomic하게 변경하며 owner, object generation, authority owner
generation과 durable Active allocation을 유지한다. Zero capacity와 `NEW_OWNER`, non-zero capacity와
all-Preserve 조합은 conflict이고 mutation은 0이다. Kotlin coroutine projection은 Java와 같은 request와 결과를
사용하며 새 overload나 별도 normalization method를 추가하지 않는다.

Kotlin descriptor와 provider는 Java `ZLinkObjectCapability.hasSnapshotAdapter()`를 그대로 사용한다. 이 값은 target에
해당 object kind의 Snapshot adapter가 등록되어 있는지만 나타내며 application state의 format, version이나 contract
ID를 광고하지 않는다. Kotlin 전용 descriptor field나 state contract 집합을 추가하지 않는다.

Runtime은 immutable Relocation root를 먼저 저장하고 reference·checksum·retention과 manifest digest를 검증한 뒤
Location Store의 단일 CAS로 reference를 공개한다. CAS 전에 실패하거나 CAS conflict가 발생한 committed root는
orphan이며 고정 retention과 cleanup으로 제거한다. Root 교체는 새 root 저장과 검증, Location reference CAS,
이전 root cleanup 순서다. Relocation payload 사용을 끝낼 때는 Location Store에서 reference 사용 종료를 CAS한 뒤
Relocation Store에서 payload를 삭제한다. 두 Store 사이 transaction이나 2PC는 요구하지 않는다.
Java에서 상속한 `ZLinkRelocationStored.checksumCrc32c()`는 저장된 immutable root bytes의 CRC32C(Castagnoli)를
나타내는 `0..0xFFFF_FFFFL` 범위의 `Long`이다. Kotlin runtime은 이 값을 Location authority에 publish할
u32 checksum과 비교하며 범위를 벗어난 provider 결과를 contract violation으로 처리한다.

## Exact generated JVM signature

```java
public abstract class systems.zlink.framework.kotlin.ZLinkSuspendingLocationStore implements systems.zlink.framework.locations.ZLinkLocationStore, systems.zlink.framework.locations.ZLinkClientServerLocationStore, systems.zlink.framework.locations.ZLinkFanoutLocationStore, systems.zlink.framework.locations.ZLinkPeerLocationStore, systems.zlink.framework.locations.ZLinkRouteLocationStore {
  public systems.zlink.framework.kotlin.ZLinkSuspendingLocationStore();
  public systems.zlink.framework.kotlin.ZLinkSuspendingLocationStore(kotlinx.coroutines.CoroutineScope, kotlinx.coroutines.CoroutineDispatcher);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAuthorityReadResult> read(java.lang.String, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAuthorityWriteResult> compareExchange(java.lang.String, systems.zlink.framework.locations.ZLinkAuthorityExpectation, systems.zlink.framework.locations.ZLinkAuthorityMutation, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAuthorityScanResult> list(java.lang.String, java.util.Optional<systems.zlink.framework.locations.ZLinkAuthorityScanCursor>, int, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkObjectReserveResult> reserve(systems.zlink.framework.locations.ZLinkObjectReservationRequest, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkObjectCommitResult> commit(systems.zlink.framework.locations.ZLinkObjectReservation, byte[], systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkObjectCreationCompleteResult> completeCreation(systems.zlink.framework.locations.ZLinkObjectReservation, systems.zlink.framework.locations.ZLinkObjectCreationCompletion, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkCreationTerminalReadResult> readCreationTerminal(systems.zlink.framework.locations.ZLinkCreationOperationIdentity, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkObjectAbortResult> abort(systems.zlink.framework.locations.ZLinkObjectReservation, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRelocationCapacityReserveResult> reserveRelocationCapacity(systems.zlink.framework.locations.ZLinkRelocationCapacityReservationRequest, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRelocationCapacityAbortResult> abortRelocationCapacity(systems.zlink.framework.locations.ZLinkRelocationCapacityFence, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAggregatePrepareResult> prepareAggregate(systems.zlink.framework.locations.ZLinkAggregatePrepareRequest, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAggregateCommitResult> commitAggregate(systems.zlink.framework.locations.ZLinkAggregateFence, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAggregateAbortResult> abortAggregate(systems.zlink.framework.locations.ZLinkAggregateFence, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> updateMeshNode(systems.zlink.framework.locations.ZLinkMeshNodeDescriptor, systems.zlink.framework.locations.ZLinkLocationWriteIntent);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteStatus> removeMeshNode(systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkMeshNodeDescriptor>> listMeshNodes(java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseClaimResult> claimOwnerLease(java.lang.String, java.time.Duration);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseReadResult> readOwnerLease(java.lang.String);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseRenewResult> renewOwnerLease(systems.zlink.framework.locations.ZLinkLocationOwnerToken, java.time.Duration);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseReleaseResult> releaseOwnerLease(systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public final java.util.concurrent.CompletionStage<java.lang.Long> removeAllByOwner(systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> updateClientServer(systems.zlink.framework.locations.ZLinkClientServerServerDescriptor, systems.zlink.framework.locations.ZLinkLocationWriteIntent);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteStatus> removeClientServer(systems.zlink.framework.locations.ZLinkClientServerServerDescriptorKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkClientServerServerDescriptor>> listClientServers(java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> updateFanoutPublisher(systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptor, systems.zlink.framework.locations.ZLinkLocationWriteIntent);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteStatus> removeFanoutPublisher(systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptorKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptor>> listFanoutPublishers(java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> updatePeer(systems.zlink.framework.locations.ZLinkPeerLocation, systems.zlink.framework.locations.ZLinkLocationWriteIntent);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> removePeer(systems.zlink.framework.locations.ZLinkPeerLocationKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkPeerLocation>> listPeerLocations(systems.zlink.framework.locations.ZLinkPeerLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> updateRoute(systems.zlink.framework.locations.ZLinkRouteLocation, systems.zlink.framework.locations.ZLinkLocationWriteIntent);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> removeRoute(systems.zlink.framework.locations.ZLinkRouteLocationKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRouteLocation> resolveRoute(systems.zlink.framework.locations.ZLinkRouteLocationKey);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkRouteLocation>> listRouteLocations(systems.zlink.framework.locations.ZLinkRouteLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest);
  protected abstract java.lang.Object readAuthoritySuspending(java.lang.String, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkAuthorityReadResult>);
  protected abstract java.lang.Object compareExchangeAuthoritySuspending(java.lang.String, systems.zlink.framework.locations.ZLinkAuthorityExpectation, systems.zlink.framework.locations.ZLinkAuthorityMutation, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkAuthorityWriteResult>);
  protected abstract java.lang.Object listAuthoritiesSuspending(java.lang.String, java.util.Optional<systems.zlink.framework.locations.ZLinkAuthorityScanCursor>, int, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkAuthorityScanResult>);
  protected abstract java.lang.Object reserveSuspending(systems.zlink.framework.locations.ZLinkObjectReservationRequest, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkObjectReserveResult>);
  protected abstract java.lang.Object commitSuspending(systems.zlink.framework.locations.ZLinkObjectReservation, byte[], systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkObjectCommitResult>);
  protected abstract java.lang.Object completeCreationSuspending(systems.zlink.framework.locations.ZLinkObjectReservation, systems.zlink.framework.locations.ZLinkObjectCreationCompletion, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkObjectCreationCompleteResult>);
  protected abstract java.lang.Object readCreationTerminalSuspending(systems.zlink.framework.locations.ZLinkCreationOperationIdentity, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkCreationTerminalReadResult>);
  protected abstract java.lang.Object abortSuspending(systems.zlink.framework.locations.ZLinkObjectReservation, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkObjectAbortResult>);
  protected abstract java.lang.Object reserveRelocationCapacitySuspending(systems.zlink.framework.locations.ZLinkRelocationCapacityReservationRequest, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkRelocationCapacityReserveResult>);
  protected abstract java.lang.Object abortRelocationCapacitySuspending(systems.zlink.framework.locations.ZLinkRelocationCapacityFence, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkRelocationCapacityAbortResult>);
  protected abstract java.lang.Object prepareAggregateSuspending(systems.zlink.framework.locations.ZLinkAggregatePrepareRequest, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkAggregatePrepareResult>);
  protected abstract java.lang.Object commitAggregateSuspending(systems.zlink.framework.locations.ZLinkAggregateFence, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkAggregateCommitResult>);
  protected abstract java.lang.Object abortAggregateSuspending(systems.zlink.framework.locations.ZLinkAggregateFence, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkAggregateAbortResult>);
  protected abstract java.lang.Object updateMeshNodeSuspending(systems.zlink.framework.locations.ZLinkMeshNodeDescriptor, systems.zlink.framework.locations.ZLinkLocationWriteIntent, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationWriteResult>);
  protected abstract java.lang.Object removeMeshNodeSuspending(systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationWriteStatus>);
  protected abstract java.lang.Object listMeshNodesSuspending(java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkMeshNodeDescriptor>>);
  protected abstract java.lang.Object claimOwnerLeaseSuspending(java.lang.String, java.time.Duration, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkOwnerLeaseClaimResult>);
  protected abstract java.lang.Object readOwnerLeaseSuspending(java.lang.String, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkOwnerLeaseReadResult>);
  protected abstract java.lang.Object renewOwnerLeaseSuspending(systems.zlink.framework.locations.ZLinkLocationOwnerToken, java.time.Duration, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkOwnerLeaseRenewResult>);
  protected abstract java.lang.Object releaseOwnerLeaseSuspending(systems.zlink.framework.locations.ZLinkLocationOwnerToken, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkOwnerLeaseReleaseResult>);
  protected abstract java.lang.Object removeAllByOwnerSuspending(systems.zlink.framework.locations.ZLinkLocationOwnerToken, kotlin.coroutines.Continuation<? super java.lang.Long>);
  protected abstract java.lang.Object updateClientServerSuspending(systems.zlink.framework.locations.ZLinkClientServerServerDescriptor, systems.zlink.framework.locations.ZLinkLocationWriteIntent, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationWriteResult>);
  protected abstract java.lang.Object removeClientServerSuspending(systems.zlink.framework.locations.ZLinkClientServerServerDescriptorKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationWriteStatus>);
  protected abstract java.lang.Object listClientServersSuspending(java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkClientServerServerDescriptor>>);
  protected abstract java.lang.Object updateFanoutPublisherSuspending(systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptor, systems.zlink.framework.locations.ZLinkLocationWriteIntent, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationWriteResult>);
  protected abstract java.lang.Object removeFanoutPublisherSuspending(systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptorKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationWriteStatus>);
  protected abstract java.lang.Object listFanoutPublishersSuspending(java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptor>>);
  protected abstract java.lang.Object updatePeerSuspending(systems.zlink.framework.locations.ZLinkPeerLocation, systems.zlink.framework.locations.ZLinkLocationWriteIntent, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationWriteResult>);
  protected abstract java.lang.Object removePeerSuspending(systems.zlink.framework.locations.ZLinkPeerLocationKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationWriteResult>);
  protected abstract java.lang.Object listPeerLocationsSuspending(systems.zlink.framework.locations.ZLinkPeerLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkPeerLocation>>);
  protected abstract java.lang.Object updateRouteSuspending(systems.zlink.framework.locations.ZLinkRouteLocation, systems.zlink.framework.locations.ZLinkLocationWriteIntent, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationWriteResult>);
  protected abstract java.lang.Object removeRouteSuspending(systems.zlink.framework.locations.ZLinkRouteLocationKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationWriteResult>);
  protected abstract java.lang.Object resolveRouteSuspending(systems.zlink.framework.locations.ZLinkRouteLocationKey, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkRouteLocation>);
  protected abstract java.lang.Object listRouteLocationsSuspending(systems.zlink.framework.locations.ZLinkRouteLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkRouteLocation>>);
}
public abstract class systems.zlink.framework.kotlin.ZLinkSuspendingRelocationStore implements systems.zlink.framework.locations.ZLinkRelocationStore {
  public systems.zlink.framework.kotlin.ZLinkSuspendingRelocationStore();
  public systems.zlink.framework.kotlin.ZLinkSuspendingRelocationStore(kotlinx.coroutines.CoroutineScope, kotlinx.coroutines.CoroutineDispatcher);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRelocationStored> put(byte[], java.time.Duration, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRelocationReadResult> get(java.lang.String, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRelocationRenewResult> renew(java.lang.String, java.time.Duration, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRelocationDeleteResult> delete(java.lang.String, systems.zlink.framework.locations.ZLinkStoreCancellation);
  protected abstract java.lang.Object putSuspending(byte[], java.time.Duration, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkRelocationStored>);
  protected abstract java.lang.Object getSuspending(java.lang.String, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkRelocationReadResult>);
  protected abstract java.lang.Object renewSuspending(java.lang.String, java.time.Duration, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkRelocationRenewResult>);
  protected abstract java.lang.Object deleteSuspending(java.lang.String, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkRelocationDeleteResult>);
}
```

Provider에 전달한 `ByteArray`는 `CompletionStage` 완료까지 immutable하다. Provider가 그 뒤 보관하려면 완료
전에 복사한다. Cursor는 opaque하고 first page는 empty `Optional`을 사용한다. Kotlin은 [Spot](../../../../01-glossary.ko.md#spot) handle resolver,
Actor directory, slot acquire/release/provider와 unbounded object list extension을 제공하지 않는다. 운영 조회는
Java의 bounded page operation을 직접 사용한다. Route miss는 negative cache에 저장하지 않는다.

공식 Redis extension도 Java의 `ZLinkRedisLocationStore`와 `ZLinkRedisRelocationStore`를 별도 instance로 사용한다.
같은 Redis deployment를 서로 다른 prefix로 사용할 수 있지만 한 instance가 두 capability를 함께 구현하지 않는다.
Redis creation-terminal key의 RID segment는 transport `RoutingId`의 exact raw bytes 길이와 그 raw bytes의
lowercase hex를 사용한다. Canonical hex text를 UTF-8로 다시 encode하지 않는다. Raw bytes가 `node-a`이면
segment는 `6:6e6f64652d61`이다.
Published reference의 permanent missing 또는 checksum·inventory digest mismatch는 `RelocationDataLost`이며 이전 owner로
rollback하지 않는다.
