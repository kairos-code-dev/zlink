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

    final override fun renewOwnerLease(
        ownerId: String,
        nodeRid: RoutingId,
        leaseTtl: Duration,
    ): CompletionStage<ZLinkOwnerLeaseRenewal> =
        async { renewOwnerLeaseSuspending(ownerId, nodeRid, leaseTtl) }

    final override fun removeOwnerLease(ownerId: String): CompletionStage<Boolean> =
        async { removeOwnerLeaseSuspending(ownerId) }

    final override fun removeAllByOwner(ownerId: String): CompletionStage<Long> =
        async { removeAllByOwnerSuspending(ownerId) }

    final override fun listOwnerLeases(): CompletionStage<ZLinkOwnerLeaseSnapshot> =
        async { listOwnerLeasesSuspending() }

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

    protected abstract suspend fun renewOwnerLeaseSuspending(
        ownerId: String,
        nodeRid: RoutingId,
        leaseTtl: Duration,
    ): ZLinkOwnerLeaseRenewal

    protected abstract suspend fun removeOwnerLeaseSuspending(ownerId: String): Boolean

    protected abstract suspend fun removeAllByOwnerSuspending(ownerId: String): Long

    protected abstract suspend fun listOwnerLeasesSuspending(): ZLinkOwnerLeaseSnapshot
}

private fun dispatcherScope(dispatcher: CoroutineDispatcher): CoroutineScope =
    object : CoroutineScope {
        override val coroutineContext: CoroutineContext = dispatcher
    }
