@file:JvmName("ZLinkLocationExtensionsKt")
@file:JvmMultifileClass

package systems.zlink.framework.kotlin

import java.time.Duration
import java.util.concurrent.CompletionStage
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlin.coroutines.CoroutineContext
import kotlinx.coroutines.future.future
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.locations.ZLinkActorLocation
import systems.zlink.framework.locations.ZLinkActorLocationFilter
import systems.zlink.framework.locations.ZLinkActorLocationKey
import systems.zlink.framework.locations.ZLinkLocationOwnerToken
import systems.zlink.framework.locations.ZLinkLocationPage
import systems.zlink.framework.locations.ZLinkLocationStore
import systems.zlink.framework.locations.ZLinkLocationWriteIntent
import systems.zlink.framework.locations.ZLinkLocationWriteResult
import systems.zlink.framework.locations.ZLinkOwnerLeaseRenewal
import systems.zlink.framework.locations.ZLinkOwnerLeaseSnapshot
import systems.zlink.framework.locations.ZLinkPageRequest
import systems.zlink.framework.locations.ZLinkPeerLocation
import systems.zlink.framework.locations.ZLinkPeerLocationFilter
import systems.zlink.framework.locations.ZLinkPeerLocationKey
import systems.zlink.framework.locations.ZLinkRouteLocation
import systems.zlink.framework.locations.ZLinkRouteLocationFilter
import systems.zlink.framework.locations.ZLinkRouteLocationKey
import systems.zlink.framework.locations.ZLinkSpotLocation
import systems.zlink.framework.locations.ZLinkSpotLocationFilter
import systems.zlink.framework.locations.ZLinkSpotLocationKey

abstract class ZLinkSuspendingLocationStore(
    private val scope: CoroutineScope = dispatcherScope(Dispatchers.IO),
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

private fun dispatcherScope(dispatcher: CoroutineDispatcher): CoroutineScope =
    object : CoroutineScope {
        override val coroutineContext: CoroutineContext = dispatcher
    }
