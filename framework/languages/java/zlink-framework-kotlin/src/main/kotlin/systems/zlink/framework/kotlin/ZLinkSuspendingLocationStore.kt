@file:JvmName("ZLinkLocationExtensionsKt")
@file:JvmMultifileClass

package systems.zlink.framework.kotlin

import java.time.Duration
import java.util.Optional
import java.util.concurrent.CompletionStage
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.future.future
import systems.zlink.framework.locations.ZLinkAggregateAbortResult
import systems.zlink.framework.locations.ZLinkAggregateCommitResult
import systems.zlink.framework.locations.ZLinkAggregateFence
import systems.zlink.framework.locations.ZLinkAggregatePrepareRequest
import systems.zlink.framework.locations.ZLinkAggregatePrepareResult
import systems.zlink.framework.locations.ZLinkActorLocation
import systems.zlink.framework.locations.ZLinkActorLocationFilter
import systems.zlink.framework.locations.ZLinkActorLocationKey
import systems.zlink.framework.locations.ZLinkLocationOwnerToken
import systems.zlink.framework.locations.ZLinkLocationPage
import systems.zlink.framework.locations.ZLinkLocationStore
import systems.zlink.framework.locations.ZLinkLocationWriteIntent
import systems.zlink.framework.locations.ZLinkLocationWriteResult
import systems.zlink.framework.locations.ZLinkLocationWriteStatus
import systems.zlink.framework.locations.ZLinkMeshNodeDescriptor
import systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey
import systems.zlink.framework.locations.ZLinkAuthorityExpectation
import systems.zlink.framework.locations.ZLinkAuthorityMutation
import systems.zlink.framework.locations.ZLinkAuthorityReadResult
import systems.zlink.framework.locations.ZLinkAuthorityScanCursor
import systems.zlink.framework.locations.ZLinkAuthorityScanResult
import systems.zlink.framework.locations.ZLinkAuthorityWriteResult
import systems.zlink.framework.locations.ZLinkObjectAbortResult
import systems.zlink.framework.locations.ZLinkObjectCommitResult
import systems.zlink.framework.locations.ZLinkObjectReservation
import systems.zlink.framework.locations.ZLinkObjectReservationRequest
import systems.zlink.framework.locations.ZLinkObjectReserveResult
import systems.zlink.framework.locations.ZLinkOwnerLeaseClaimResult
import systems.zlink.framework.locations.ZLinkOwnerLeaseReadResult
import systems.zlink.framework.locations.ZLinkOwnerLeaseReleaseResult
import systems.zlink.framework.locations.ZLinkOwnerLeaseRenewResult
import systems.zlink.framework.locations.ZLinkPageRequest
import systems.zlink.framework.locations.ZLinkPeerLocation
import systems.zlink.framework.locations.ZLinkPeerLocationFilter
import systems.zlink.framework.locations.ZLinkPeerLocationKey
import systems.zlink.framework.locations.ZLinkRouteLocation
import systems.zlink.framework.locations.ZLinkRouteLocationFilter
import systems.zlink.framework.locations.ZLinkRouteLocationKey
import systems.zlink.framework.locations.ZLinkRelocationCapacityAbortResult
import systems.zlink.framework.locations.ZLinkRelocationCapacityFence
import systems.zlink.framework.locations.ZLinkRelocationCapacityReservationRequest
import systems.zlink.framework.locations.ZLinkRelocationCapacityReserveResult
import systems.zlink.framework.locations.ZLinkSpotLocation
import systems.zlink.framework.locations.ZLinkSpotLocationFilter
import systems.zlink.framework.locations.ZLinkSpotLocationKey
import systems.zlink.framework.locations.ZLinkStoreCancellation

abstract class ZLinkSuspendingLocationStore(
    private val scope: CoroutineScope = dispatcherScope(Dispatchers.IO),
    private val dispatcher: CoroutineDispatcher = Dispatchers.IO,
) : ZLinkLocationStore {
    protected fun <T> async(block: suspend () -> T): CompletionStage<T> =
        scope.future(dispatcher) { block() }

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

    final override fun listPeerLocations(filter: ZLinkPeerLocationFilter): CompletionStage<List<ZLinkPeerLocation>> =
        async { listPeerLocationsSuspending(filter) }

    final override fun updateSpot(
        spot: ZLinkSpotLocation,
        intent: ZLinkLocationWriteIntent,
    ): CompletionStage<ZLinkLocationWriteResult> =
        async { updateSpotSuspending(spot, intent) }

    final override fun removeSpot(
        key: ZLinkSpotLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): CompletionStage<ZLinkLocationWriteResult> =
        async { removeSpotSuspending(key, owner) }

    final override fun resolveSpot(key: ZLinkSpotLocationKey): CompletionStage<ZLinkSpotLocation?> =
        async { resolveSpotSuspending(key) }

    final override fun listSpotLocations(
        filter: ZLinkSpotLocationFilter,
        page: ZLinkPageRequest,
    ): CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> =
        async { listSpotLocationsSuspending(filter, page) }

    final override fun updateActor(
        actor: ZLinkActorLocation,
        intent: ZLinkLocationWriteIntent,
    ): CompletionStage<ZLinkLocationWriteResult> =
        async { updateActorSuspending(actor, intent) }

    final override fun removeActor(
        key: ZLinkActorLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): CompletionStage<ZLinkLocationWriteResult> =
        async { removeActorSuspending(key, owner) }

    final override fun resolveActor(key: ZLinkActorLocationKey): CompletionStage<ZLinkActorLocation?> =
        async { resolveActorSuspending(key) }

    final override fun listActorLocations(
        filter: ZLinkActorLocationFilter,
        page: ZLinkPageRequest,
    ): CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> =
        async { listActorLocationsSuspending(filter, page) }

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

    final override fun resolveRoute(key: ZLinkRouteLocationKey): CompletionStage<ZLinkRouteLocation?> =
        async { resolveRouteSuspending(key) }

    final override fun listRouteLocations(
        filter: ZLinkRouteLocationFilter,
        page: ZLinkPageRequest,
    ): CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> =
        async { listRouteLocationsSuspending(filter, page) }

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

    final override fun removeAllByOwner(ownerId: String): CompletionStage<Long> =
        async { removeAllByOwnerSuspending(ownerId) }

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

    protected abstract suspend fun updatePeerSuspending(
        peer: ZLinkPeerLocation,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun removePeerSuspending(
        key: ZLinkPeerLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun listPeerLocationsSuspending(filter: ZLinkPeerLocationFilter): List<ZLinkPeerLocation>

    protected abstract suspend fun updateSpotSuspending(
        spot: ZLinkSpotLocation,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun removeSpotSuspending(
        key: ZLinkSpotLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun resolveSpotSuspending(key: ZLinkSpotLocationKey): ZLinkSpotLocation?

    protected abstract suspend fun listSpotLocationsSuspending(
        filter: ZLinkSpotLocationFilter,
        page: ZLinkPageRequest,
    ): ZLinkLocationPage<ZLinkSpotLocation>

    protected abstract suspend fun updateActorSuspending(
        actor: ZLinkActorLocation,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun removeActorSuspending(
        key: ZLinkActorLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun resolveActorSuspending(key: ZLinkActorLocationKey): ZLinkActorLocation?

    protected abstract suspend fun listActorLocationsSuspending(
        filter: ZLinkActorLocationFilter,
        page: ZLinkPageRequest,
    ): ZLinkLocationPage<ZLinkActorLocation>

    protected abstract suspend fun updateRouteSuspending(
        route: ZLinkRouteLocation,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun removeRouteSuspending(
        key: ZLinkRouteLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun resolveRouteSuspending(key: ZLinkRouteLocationKey): ZLinkRouteLocation?

    protected abstract suspend fun listRouteLocationsSuspending(
        filter: ZLinkRouteLocationFilter,
        page: ZLinkPageRequest,
    ): ZLinkLocationPage<ZLinkRouteLocation>

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

    protected abstract suspend fun removeAllByOwnerSuspending(ownerId: String): Long
}

private fun dispatcherScope(dispatcher: CoroutineDispatcher): CoroutineScope =
    object : CoroutineScope {
        override val coroutineContext: CoroutineContext = dispatcher
    }
