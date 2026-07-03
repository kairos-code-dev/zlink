package systems.zlink.framework.kotlin

import java.time.Duration
import java.util.concurrent.CompletionStage
import java.util.concurrent.Flow.Publisher
import java.util.concurrent.Flow.Subscriber
import java.util.concurrent.Flow.Subscription
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.future.await
import kotlinx.coroutines.future.future
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.locations.ZLinkActorLocation
import systems.zlink.framework.locations.ZLinkActorLocationFilter
import systems.zlink.framework.locations.ZLinkActorLocationKey
import systems.zlink.framework.locations.ZLinkActorLocationResolver
import systems.zlink.framework.locations.ZLinkActorLocationStore
import systems.zlink.framework.locations.ZLinkLocationChanged
import systems.zlink.framework.locations.ZLinkLocationOwnerToken
import systems.zlink.framework.locations.ZLinkLocationPage
import systems.zlink.framework.locations.ZLinkLocationRuntimeQuery
import systems.zlink.framework.locations.ZLinkLocationRuntimeStatus
import systems.zlink.framework.locations.ZLinkLocationServiceSummary
import systems.zlink.framework.locations.ZLinkLocationServiceSummaryFilter
import systems.zlink.framework.locations.ZLinkLocationStore
import systems.zlink.framework.locations.ZLinkLocationTopologyEntry
import systems.zlink.framework.locations.ZLinkLocationTopologyFilter
import systems.zlink.framework.locations.ZLinkLocationWatchFilter
import systems.zlink.framework.locations.ZLinkLocationWatchStore
import systems.zlink.framework.locations.ZLinkLocationWriteIntent
import systems.zlink.framework.locations.ZLinkLocationWriteResult
import systems.zlink.framework.locations.ZLinkOwnerLeaseSnapshot
import systems.zlink.framework.locations.ZLinkPageRequest
import systems.zlink.framework.locations.ZLinkPeerLocation
import systems.zlink.framework.locations.ZLinkPeerLocationFilter
import systems.zlink.framework.locations.ZLinkPeerLocationKey
import systems.zlink.framework.locations.ZLinkPeerLocationResolver
import systems.zlink.framework.locations.ZLinkPeerLocationStore
import systems.zlink.framework.locations.ZLinkRouteLocation
import systems.zlink.framework.locations.ZLinkRouteLocationFilter
import systems.zlink.framework.locations.ZLinkRouteLocationKey
import systems.zlink.framework.locations.ZLinkRouteLocationResolver
import systems.zlink.framework.locations.ZLinkRouteLocationStore
import systems.zlink.framework.locations.ZLinkSpotAddress
import systems.zlink.framework.locations.ZLinkSpotLocation
import systems.zlink.framework.locations.ZLinkSpotLocationFilter
import systems.zlink.framework.locations.ZLinkSpotLocationKey
import systems.zlink.framework.locations.ZLinkSpotLocationResolver
import systems.zlink.framework.locations.ZLinkSpotLocationStore

suspend fun ZLinkPeerLocationStore.updatePeer(
    peer: ZLinkPeerLocation,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult =
    updatePeerAsync(peer, intent).await()

suspend fun ZLinkPeerLocationStore.removePeer(
    key: ZLinkPeerLocationKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteResult =
    removePeerAsync(key, owner).await()

suspend fun ZLinkPeerLocationStore.removePeersByOwner(ownerId: String): Long =
    removePeersByOwnerAsync(ownerId).await()

suspend fun ZLinkPeerLocationStore.listPeers(filter: ZLinkPeerLocationFilter): List<ZLinkPeerLocation> =
    listPeersAsync(filter).await()

suspend fun ZLinkSpotLocationStore.updateSpot(
    spot: ZLinkSpotLocation,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult =
    updateSpotAsync(spot, intent).await()

suspend fun ZLinkSpotLocationStore.removeSpot(
    key: ZLinkSpotLocationKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteResult =
    removeSpotAsync(key, owner).await()

suspend fun ZLinkSpotLocationStore.removeSpotsByOwner(ownerId: String): Long =
    removeSpotsByOwnerAsync(ownerId).await()

suspend fun ZLinkSpotLocationStore.resolveSpot(key: ZLinkSpotLocationKey): ZLinkSpotLocation? =
    resolveSpotAsync(key).await()

suspend fun ZLinkSpotLocationStore.listSpots(
    filter: ZLinkSpotLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkSpotLocation> =
    listSpotsAsync(filter, page).await()

suspend fun ZLinkActorLocationStore.updateActor(
    actor: ZLinkActorLocation,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult =
    updateActorAsync(actor, intent).await()

suspend fun ZLinkActorLocationStore.removeActor(
    key: ZLinkActorLocationKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteResult =
    removeActorAsync(key, owner).await()

suspend fun ZLinkActorLocationStore.removeActorsByOwner(ownerId: String): Long =
    removeActorsByOwnerAsync(ownerId).await()

suspend fun ZLinkActorLocationStore.resolveActor(key: ZLinkActorLocationKey): ZLinkActorLocation? =
    resolveActorAsync(key).await()

suspend fun ZLinkActorLocationStore.listActors(
    filter: ZLinkActorLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkActorLocation> =
    listActorsAsync(filter, page).await()

suspend fun ZLinkRouteLocationStore.updateRoute(
    route: ZLinkRouteLocation,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult =
    updateRouteAsync(route, intent).await()

suspend fun ZLinkRouteLocationStore.removeRoute(
    key: ZLinkRouteLocationKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteResult =
    removeRouteAsync(key, owner).await()

suspend fun ZLinkRouteLocationStore.removeRoutesByOwner(ownerId: String): Long =
    removeRoutesByOwnerAsync(ownerId).await()

suspend fun ZLinkRouteLocationStore.resolveRoute(key: ZLinkRouteLocationKey): ZLinkRouteLocation? =
    resolveRouteAsync(key).await()

suspend fun ZLinkRouteLocationStore.listRoutes(
    filter: ZLinkRouteLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkRouteLocation> =
    listRoutesAsync(filter, page).await()

suspend fun ZLinkLocationStore.renewOwnerLease(
    ownerId: String,
    nodeRid: RoutingId,
    leaseTtl: Duration,
): ZLinkLocationWriteResult =
    renewOwnerLeaseAsync(ownerId, nodeRid, leaseTtl).await()

suspend fun ZLinkLocationStore.removeOwnerLease(ownerId: String): ZLinkLocationWriteResult =
    removeOwnerLeaseAsync(ownerId).await()

suspend fun ZLinkLocationStore.listOwnerLeases(): ZLinkOwnerLeaseSnapshot =
    listOwnerLeasesAsync().await()

suspend fun ZLinkPeerLocationResolver.listPeers(filter: ZLinkPeerLocationFilter): List<ZLinkPeerLocation> =
    listPeersAsync(filter).await()

suspend fun ZLinkSpotLocationResolver.resolveSpotAddress(
    meshName: String,
    spotRid: RoutingId,
): ZLinkSpotAddress? =
    resolveSpotAddressAsync(meshName, spotRid).await()

suspend fun ZLinkActorLocationResolver.resolveActorSpotAddress(
    actorType: String,
    actorId: String,
): ZLinkSpotAddress? =
    resolveActorSpotAddressAsync(actorType, actorId).await()

suspend fun ZLinkRouteLocationResolver.resolveRoute(key: ZLinkRouteLocationKey): ZLinkRouteLocation? =
    resolveRouteAsync(key).await()

suspend fun ZLinkLocationRuntimeQuery.status(): ZLinkLocationRuntimeStatus =
    getStatusAsync().await()

suspend fun ZLinkLocationRuntimeQuery.listPeers(filter: ZLinkPeerLocationFilter): List<ZLinkPeerLocation> =
    listPeersAsync(filter).await()

suspend fun ZLinkLocationRuntimeQuery.listSpots(
    filter: ZLinkSpotLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkSpotLocation> =
    listSpotsAsync(filter, page).await()

suspend fun ZLinkLocationRuntimeQuery.listActors(
    filter: ZLinkActorLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkActorLocation> =
    listActorsAsync(filter, page).await()

suspend fun ZLinkLocationRuntimeQuery.listRoutes(
    filter: ZLinkRouteLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkRouteLocation> =
    listRoutesAsync(filter, page).await()

suspend fun ZLinkLocationRuntimeQuery.listTopology(
    filter: ZLinkLocationTopologyFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkLocationTopologyEntry> =
    listTopologyAsync(filter, page).await()

suspend fun ZLinkLocationRuntimeQuery.listServiceSummaries(
    filter: ZLinkLocationServiceSummaryFilter,
): List<ZLinkLocationServiceSummary> =
    listServiceSummariesAsync(filter).await()

fun <T> locationPages(
    firstPage: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
    load: (ZLinkPageRequest) -> CompletionStage<ZLinkLocationPage<T>>,
): Flow<T> = flow {
    var request = firstPage
    while (true) {
        val page = load(request).await()
        for (item in page.items()) {
            emit(item)
        }
        val next = page.continuationToken()
        if (next.isNullOrBlank()) {
            break
        }
        request = ZLinkPageRequest(request.pageSize(), next)
    }
}

fun ZLinkLocationRuntimeQuery.spots(
    filter: ZLinkSpotLocationFilter,
    pageSize: Int,
): Flow<ZLinkSpotLocation> =
    locationPages(ZLinkPageRequest(pageSize, null)) { page -> listSpotsAsync(filter, page) }

fun ZLinkLocationRuntimeQuery.actors(
    filter: ZLinkActorLocationFilter,
    pageSize: Int,
): Flow<ZLinkActorLocation> =
    locationPages(ZLinkPageRequest(pageSize, null)) { page -> listActorsAsync(filter, page) }

fun ZLinkLocationRuntimeQuery.routes(
    filter: ZLinkRouteLocationFilter,
    pageSize: Int,
): Flow<ZLinkRouteLocation> =
    locationPages(ZLinkPageRequest(pageSize, null)) { page -> listRoutesAsync(filter, page) }

fun ZLinkLocationRuntimeQuery.topology(
    filter: ZLinkLocationTopologyFilter,
    pageSize: Int,
): Flow<ZLinkLocationTopologyEntry> =
    locationPages(ZLinkPageRequest(pageSize, null)) { page -> listTopologyAsync(filter, page) }

fun ZLinkLocationWatchStore.changes(filter: ZLinkLocationWatchFilter): Flow<ZLinkLocationChanged> =
    watch(filter).asFlow()

fun <T> Publisher<T>.asFlow(): Flow<T> = callbackFlow {
    val subscriber = object : Subscriber<T> {
        private var subscription: Subscription? = null

        override fun onSubscribe(subscription: Subscription) {
            this.subscription = subscription
            subscription.request(Long.MAX_VALUE)
        }

        override fun onNext(item: T) {
            trySend(item)
        }

        override fun onError(throwable: Throwable) {
            close(throwable)
        }

        override fun onComplete() {
            close()
        }

        fun cancel() {
            subscription?.cancel()
        }
    }
    subscribe(subscriber)
    awaitClose { subscriber.cancel() }
}

abstract class ZLinkSuspendingLocationStore(
    private val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Default),
    private val dispatcher: CoroutineDispatcher = Dispatchers.Default,
) : ZLinkLocationStore {
    protected fun <T> async(block: suspend () -> T): CompletionStage<T> =
        scope.future(dispatcher) { block() }

    final override fun updatePeerAsync(
        peer: ZLinkPeerLocation,
        intent: ZLinkLocationWriteIntent,
    ): CompletionStage<ZLinkLocationWriteResult> =
        async { updatePeer(peer, intent) }

    final override fun removePeerAsync(
        key: ZLinkPeerLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): CompletionStage<ZLinkLocationWriteResult> =
        async { removePeer(key, owner) }

    final override fun removePeersByOwnerAsync(ownerId: String): CompletionStage<Long> =
        async { removePeersByOwner(ownerId) }

    final override fun listPeersAsync(filter: ZLinkPeerLocationFilter): CompletionStage<List<ZLinkPeerLocation>> =
        async { listPeers(filter) }

    final override fun updateSpotAsync(
        spot: ZLinkSpotLocation,
        intent: ZLinkLocationWriteIntent,
    ): CompletionStage<ZLinkLocationWriteResult> =
        async { updateSpot(spot, intent) }

    final override fun removeSpotAsync(
        key: ZLinkSpotLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): CompletionStage<ZLinkLocationWriteResult> =
        async { removeSpot(key, owner) }

    final override fun removeSpotsByOwnerAsync(ownerId: String): CompletionStage<Long> =
        async { removeSpotsByOwner(ownerId) }

    final override fun resolveSpotAsync(key: ZLinkSpotLocationKey): CompletionStage<ZLinkSpotLocation?> =
        async { resolveSpot(key) }

    final override fun listSpotsAsync(
        filter: ZLinkSpotLocationFilter,
        page: ZLinkPageRequest,
    ): CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> =
        async { listSpots(filter, page) }

    final override fun updateActorAsync(
        actor: ZLinkActorLocation,
        intent: ZLinkLocationWriteIntent,
    ): CompletionStage<ZLinkLocationWriteResult> =
        async { updateActor(actor, intent) }

    final override fun removeActorAsync(
        key: ZLinkActorLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): CompletionStage<ZLinkLocationWriteResult> =
        async { removeActor(key, owner) }

    final override fun removeActorsByOwnerAsync(ownerId: String): CompletionStage<Long> =
        async { removeActorsByOwner(ownerId) }

    final override fun resolveActorAsync(key: ZLinkActorLocationKey): CompletionStage<ZLinkActorLocation?> =
        async { resolveActor(key) }

    final override fun listActorsAsync(
        filter: ZLinkActorLocationFilter,
        page: ZLinkPageRequest,
    ): CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> =
        async { listActors(filter, page) }

    final override fun updateRouteAsync(
        route: ZLinkRouteLocation,
        intent: ZLinkLocationWriteIntent,
    ): CompletionStage<ZLinkLocationWriteResult> =
        async { updateRoute(route, intent) }

    final override fun removeRouteAsync(
        key: ZLinkRouteLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): CompletionStage<ZLinkLocationWriteResult> =
        async { removeRoute(key, owner) }

    final override fun removeRoutesByOwnerAsync(ownerId: String): CompletionStage<Long> =
        async { removeRoutesByOwner(ownerId) }

    final override fun resolveRouteAsync(key: ZLinkRouteLocationKey): CompletionStage<ZLinkRouteLocation?> =
        async { resolveRoute(key) }

    final override fun listRoutesAsync(
        filter: ZLinkRouteLocationFilter,
        page: ZLinkPageRequest,
    ): CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> =
        async { listRoutes(filter, page) }

    final override fun renewOwnerLeaseAsync(
        ownerId: String,
        nodeRid: RoutingId,
        leaseTtl: Duration,
    ): CompletionStage<ZLinkLocationWriteResult> =
        async { renewOwnerLease(ownerId, nodeRid, leaseTtl) }

    final override fun removeOwnerLeaseAsync(ownerId: String): CompletionStage<ZLinkLocationWriteResult> =
        async { removeOwnerLease(ownerId) }

    final override fun listOwnerLeasesAsync(): CompletionStage<ZLinkOwnerLeaseSnapshot> =
        async { listOwnerLeases() }

    protected abstract suspend fun updatePeer(
        peer: ZLinkPeerLocation,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun removePeer(
        key: ZLinkPeerLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun removePeersByOwner(ownerId: String): Long

    protected abstract suspend fun listPeers(filter: ZLinkPeerLocationFilter): List<ZLinkPeerLocation>

    protected abstract suspend fun updateSpot(
        spot: ZLinkSpotLocation,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun removeSpot(
        key: ZLinkSpotLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun removeSpotsByOwner(ownerId: String): Long

    protected abstract suspend fun resolveSpot(key: ZLinkSpotLocationKey): ZLinkSpotLocation?

    protected abstract suspend fun listSpots(
        filter: ZLinkSpotLocationFilter,
        page: ZLinkPageRequest,
    ): ZLinkLocationPage<ZLinkSpotLocation>

    protected abstract suspend fun updateActor(
        actor: ZLinkActorLocation,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun removeActor(
        key: ZLinkActorLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun removeActorsByOwner(ownerId: String): Long

    protected abstract suspend fun resolveActor(key: ZLinkActorLocationKey): ZLinkActorLocation?

    protected abstract suspend fun listActors(
        filter: ZLinkActorLocationFilter,
        page: ZLinkPageRequest,
    ): ZLinkLocationPage<ZLinkActorLocation>

    protected abstract suspend fun updateRoute(
        route: ZLinkRouteLocation,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun removeRoute(
        key: ZLinkRouteLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun removeRoutesByOwner(ownerId: String): Long

    protected abstract suspend fun resolveRoute(key: ZLinkRouteLocationKey): ZLinkRouteLocation?

    protected abstract suspend fun listRoutes(
        filter: ZLinkRouteLocationFilter,
        page: ZLinkPageRequest,
    ): ZLinkLocationPage<ZLinkRouteLocation>

    protected abstract suspend fun renewOwnerLease(
        ownerId: String,
        nodeRid: RoutingId,
        leaseTtl: Duration,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun removeOwnerLease(ownerId: String): ZLinkLocationWriteResult

    protected abstract suspend fun listOwnerLeases(): ZLinkOwnerLeaseSnapshot
}
