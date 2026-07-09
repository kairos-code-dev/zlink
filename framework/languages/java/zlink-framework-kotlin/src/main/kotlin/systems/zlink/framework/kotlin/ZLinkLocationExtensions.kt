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
import kotlinx.coroutines.future.future
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.locations.ActorSpotRefResolver
import systems.zlink.framework.locations.SpotRef
import systems.zlink.framework.locations.SpotRefResolver
import systems.zlink.framework.locations.ZLinkActorLocation
import systems.zlink.framework.locations.ZLinkActorLocationFilter
import systems.zlink.framework.locations.ZLinkActorLocationKey
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
import systems.zlink.framework.locations.ZLinkOwnerLeaseRenewal
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
import systems.zlink.framework.locations.ZLinkRouteLocationStore
import systems.zlink.framework.locations.ZLinkSpotLocation
import systems.zlink.framework.locations.ZLinkSpotLocationFilter
import systems.zlink.framework.locations.ZLinkSpotLocationKey
import systems.zlink.framework.locations.ZLinkSpotLocationStore

suspend fun ZLinkPeerLocationStore.updatePeer(
    peer: ZLinkPeerLocation,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult =
    awaitFrameworkStage(updatePeerAsync(peer, intent))

suspend fun ZLinkPeerLocationStore.removePeer(
    key: ZLinkPeerLocationKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteResult =
    awaitFrameworkStage(removePeerAsync(key, owner))

suspend fun ZLinkPeerLocationStore.listPeerLocations(filter: ZLinkPeerLocationFilter): List<ZLinkPeerLocation> =
    awaitFrameworkStage(listPeerLocationsAsync(filter))

suspend fun ZLinkSpotLocationStore.updateSpot(
    spot: ZLinkSpotLocation,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult =
    awaitFrameworkStage(updateSpotAsync(spot, intent))

suspend fun ZLinkSpotLocationStore.removeSpot(
    key: ZLinkSpotLocationKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteResult =
    awaitFrameworkStage(removeSpotAsync(key, owner))

suspend fun ZLinkSpotLocationStore.resolveSpot(key: ZLinkSpotLocationKey): ZLinkSpotLocation? =
    awaitFrameworkStage(resolveSpotAsync(key))

suspend fun ZLinkSpotLocationStore.listSpotLocations(
    filter: ZLinkSpotLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkSpotLocation> =
    awaitFrameworkStage(listSpotLocationsAsync(filter, page))

suspend fun ZLinkActorLocationStore.updateActor(
    actor: ZLinkActorLocation,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult =
    awaitFrameworkStage(updateActorAsync(actor, intent))

suspend fun ZLinkActorLocationStore.removeActor(
    key: ZLinkActorLocationKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteResult =
    awaitFrameworkStage(removeActorAsync(key, owner))

suspend fun ZLinkActorLocationStore.resolveActor(key: ZLinkActorLocationKey): ZLinkActorLocation? =
    awaitFrameworkStage(resolveActorAsync(key))

suspend fun ZLinkActorLocationStore.listActorLocations(
    filter: ZLinkActorLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkActorLocation> =
    awaitFrameworkStage(listActorLocationsAsync(filter, page))

suspend fun ZLinkRouteLocationStore.updateRoute(
    route: ZLinkRouteLocation,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult =
    awaitFrameworkStage(updateRouteAsync(route, intent))

suspend fun ZLinkRouteLocationStore.removeRoute(
    key: ZLinkRouteLocationKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteResult =
    awaitFrameworkStage(removeRouteAsync(key, owner))

suspend fun ZLinkRouteLocationStore.resolveRoute(key: ZLinkRouteLocationKey): ZLinkRouteLocation? =
    awaitFrameworkStage(resolveRouteAsync(key))

suspend fun ZLinkRouteLocationStore.listRouteLocations(
    filter: ZLinkRouteLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkRouteLocation> =
    awaitFrameworkStage(listRouteLocationsAsync(filter, page))

suspend fun ZLinkLocationStore.renewOwnerLease(
    ownerId: String,
    nodeRid: RoutingId,
    leaseTtl: Duration,
): ZLinkOwnerLeaseRenewal =
    awaitFrameworkStage(renewOwnerLeaseAsync(ownerId, nodeRid, leaseTtl))

suspend fun ZLinkLocationStore.removeOwnerLease(ownerId: String): Boolean =
    awaitFrameworkStage(removeOwnerLeaseAsync(ownerId))

suspend fun ZLinkLocationStore.removeAllByOwner(ownerId: String): Long =
    awaitFrameworkStage(removeAllByOwnerAsync(ownerId))

suspend fun ZLinkLocationStore.listOwnerLeases(): ZLinkOwnerLeaseSnapshot =
    awaitFrameworkStage(listOwnerLeasesAsync())

suspend fun ZLinkPeerLocationResolver.listLivePeers(filter: ZLinkPeerLocationFilter): List<ZLinkPeerLocation> =
    awaitFrameworkStage(listLivePeersAsync(filter))

suspend fun SpotRefResolver.resolveSpotRef(
    meshName: String,
    spotRid: RoutingId,
): SpotRef? =
    awaitFrameworkStage(resolveSpotRefAsync(meshName, spotRid))

suspend fun ActorSpotRefResolver.resolveActorSpotRef(
    actorId: String,
): SpotRef? =
    awaitFrameworkStage(resolveActorSpotRefAsync(actorId))

suspend fun ZLinkLocationRuntimeQuery.status(): ZLinkLocationRuntimeStatus =
    awaitFrameworkStage(getStatusAsync())

suspend fun ZLinkLocationRuntimeQuery.listPeerLocations(filter: ZLinkPeerLocationFilter): List<ZLinkPeerLocation> =
    awaitFrameworkStage(listPeerLocationsAsync(filter))

suspend fun ZLinkLocationRuntimeQuery.listSpotLocations(
    filter: ZLinkSpotLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkSpotLocation> =
    awaitFrameworkStage(listSpotLocationsAsync(filter, page))

suspend fun ZLinkLocationRuntimeQuery.listActorLocations(
    filter: ZLinkActorLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkActorLocation> =
    awaitFrameworkStage(listActorLocationsAsync(filter, page))

suspend fun ZLinkLocationRuntimeQuery.listRouteLocations(
    filter: ZLinkRouteLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkRouteLocation> =
    awaitFrameworkStage(listRouteLocationsAsync(filter, page))

suspend fun ZLinkLocationRuntimeQuery.listTopology(
    filter: ZLinkLocationTopologyFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkLocationTopologyEntry> =
    awaitFrameworkStage(listTopologyAsync(filter, page))

suspend fun ZLinkLocationRuntimeQuery.listServiceSummaries(
    filter: ZLinkLocationServiceSummaryFilter,
): List<ZLinkLocationServiceSummary> =
    awaitFrameworkStage(listServiceSummariesAsync(filter))

fun <T> locationPages(
    firstPage: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
    load: (ZLinkPageRequest) -> CompletionStage<ZLinkLocationPage<T>>,
): Flow<T> = flow {
    var request = firstPage
    while (true) {
        val page = awaitFrameworkStage(load(request))
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
    locationPages(ZLinkPageRequest(pageSize, null)) { page -> listSpotLocationsAsync(filter, page) }

fun ZLinkLocationRuntimeQuery.actors(
    filter: ZLinkActorLocationFilter,
    pageSize: Int,
): Flow<ZLinkActorLocation> =
    locationPages(ZLinkPageRequest(pageSize, null)) { page -> listActorLocationsAsync(filter, page) }

fun ZLinkLocationRuntimeQuery.routes(
    filter: ZLinkRouteLocationFilter,
    pageSize: Int,
): Flow<ZLinkRouteLocation> =
    locationPages(ZLinkPageRequest(pageSize, null)) { page -> listRouteLocationsAsync(filter, page) }

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
    private val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO),
    private val dispatcher: CoroutineDispatcher = Dispatchers.IO,
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

    final override fun listPeerLocationsAsync(filter: ZLinkPeerLocationFilter): CompletionStage<List<ZLinkPeerLocation>> =
        async { listPeerLocations(filter) }

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

    final override fun resolveSpotAsync(key: ZLinkSpotLocationKey): CompletionStage<ZLinkSpotLocation?> =
        async { resolveSpot(key) }

    final override fun listSpotLocationsAsync(
        filter: ZLinkSpotLocationFilter,
        page: ZLinkPageRequest,
    ): CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> =
        async { listSpotLocations(filter, page) }

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

    final override fun resolveActorAsync(key: ZLinkActorLocationKey): CompletionStage<ZLinkActorLocation?> =
        async { resolveActor(key) }

    final override fun listActorLocationsAsync(
        filter: ZLinkActorLocationFilter,
        page: ZLinkPageRequest,
    ): CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> =
        async { listActorLocations(filter, page) }

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

    final override fun resolveRouteAsync(key: ZLinkRouteLocationKey): CompletionStage<ZLinkRouteLocation?> =
        async { resolveRoute(key) }

    final override fun listRouteLocationsAsync(
        filter: ZLinkRouteLocationFilter,
        page: ZLinkPageRequest,
    ): CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> =
        async { listRouteLocations(filter, page) }

    final override fun renewOwnerLeaseAsync(
        ownerId: String,
        nodeRid: RoutingId,
        leaseTtl: Duration,
    ): CompletionStage<ZLinkOwnerLeaseRenewal> =
        async { renewOwnerLease(ownerId, nodeRid, leaseTtl) }

    final override fun removeOwnerLeaseAsync(ownerId: String): CompletionStage<Boolean> =
        async { removeOwnerLease(ownerId) }

    final override fun removeAllByOwnerAsync(ownerId: String): CompletionStage<Long> =
        async { removeAllByOwner(ownerId) }

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

    protected abstract suspend fun listPeerLocations(filter: ZLinkPeerLocationFilter): List<ZLinkPeerLocation>

    protected abstract suspend fun updateSpot(
        spot: ZLinkSpotLocation,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun removeSpot(
        key: ZLinkSpotLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteResult

    protected abstract suspend fun resolveSpot(key: ZLinkSpotLocationKey): ZLinkSpotLocation?

    protected abstract suspend fun listSpotLocations(
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

    protected abstract suspend fun resolveActor(key: ZLinkActorLocationKey): ZLinkActorLocation?

    protected abstract suspend fun listActorLocations(
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

    protected abstract suspend fun resolveRoute(key: ZLinkRouteLocationKey): ZLinkRouteLocation?

    protected abstract suspend fun listRouteLocations(
        filter: ZLinkRouteLocationFilter,
        page: ZLinkPageRequest,
    ): ZLinkLocationPage<ZLinkRouteLocation>

    protected abstract suspend fun renewOwnerLease(
        ownerId: String,
        nodeRid: RoutingId,
        leaseTtl: Duration,
    ): ZLinkOwnerLeaseRenewal

    protected abstract suspend fun removeOwnerLease(ownerId: String): Boolean

    protected abstract suspend fun removeAllByOwner(ownerId: String): Long

    protected abstract suspend fun listOwnerLeases(): ZLinkOwnerLeaseSnapshot
}
