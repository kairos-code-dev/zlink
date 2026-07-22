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

    final override fun abort(
        reservation: ZLinkObjectReservation,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkObjectAbortResult> =
        async { abortSuspending(reservation, cancellation) }

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

    protected abstract suspend fun abortSuspending(
        reservation: ZLinkObjectReservation,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkObjectAbortResult

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

abstract class ZLinkSuspendingTransferStore(
    scope: CoroutineScope = dispatcherScope(Dispatchers.IO),
    dispatcher: CoroutineDispatcher = Dispatchers.IO,
) : ZLinkTransferStore {
    private fun <T> async(block: suspend () -> T): CompletionStage<T>

    final override fun put(
        payload: ByteArray,
        retention: Duration,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkTransferStored> =
        async { putSuspending(payload, retention, cancellation) }

    final override fun get(
        reference: String,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkTransferReadResult> =
        async { getSuspending(reference, cancellation) }

    final override fun renew(
        reference: String,
        retention: Duration,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkTransferRenewResult> =
        async { renewSuspending(reference, retention, cancellation) }

    final override fun delete(
        reference: String,
        cancellation: ZLinkStoreCancellation,
    ): CompletionStage<ZLinkTransferDeleteResult> =
        async { deleteSuspending(reference, cancellation) }

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

두 primary constructor의 인자는 모두 기본값이 있으므로 JVM에는 public no-arg constructor와
`CoroutineScope`, `CoroutineDispatcher`를 받는 public constructor가 생성된다. Compiler가 기본 인자를
처리하기 위해 만드는 synthetic mask constructor는 public contract에 포함하지 않는다. Java Store method는
subclass가 다시 override할 수 없는 `public final` bridge이며, provider 구현은 protected suspend hook만 구현한다.

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
Java에서 상속한 `ZLinkTransferStored.checksumCrc32c()`는 저장된 immutable root bytes의 CRC32C(Castagnoli)를
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
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkObjectAbortResult> abort(systems.zlink.framework.locations.ZLinkObjectReservation, systems.zlink.framework.locations.ZLinkStoreCancellation);
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
  protected abstract java.lang.Object abortSuspending(systems.zlink.framework.locations.ZLinkObjectReservation, systems.zlink.framework.locations.ZLinkStoreCancellation, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkObjectAbortResult>);
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
public abstract class systems.zlink.framework.kotlin.ZLinkSuspendingTransferStore implements systems.zlink.framework.locations.ZLinkTransferStore {
  public systems.zlink.framework.kotlin.ZLinkSuspendingTransferStore();
  public systems.zlink.framework.kotlin.ZLinkSuspendingTransferStore(kotlinx.coroutines.CoroutineScope, kotlinx.coroutines.CoroutineDispatcher);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkTransferStored> put(byte[], java.time.Duration, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkTransferReadResult> get(java.lang.String, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkTransferRenewResult> renew(java.lang.String, java.time.Duration, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkTransferDeleteResult> delete(java.lang.String, systems.zlink.framework.locations.ZLinkStoreCancellation);
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
