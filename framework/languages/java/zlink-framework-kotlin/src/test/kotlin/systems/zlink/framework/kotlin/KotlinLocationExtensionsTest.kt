package systems.zlink.framework.kotlin

import java.time.Duration
import java.time.Instant
import java.util.concurrent.Flow.Publisher
import java.util.concurrent.Flow.Subscriber
import java.util.concurrent.Flow.Subscription
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Test
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.locations.ZLinkActorLocation
import systems.zlink.framework.locations.ZLinkActorLocationFilter
import systems.zlink.framework.locations.ZLinkActorLocationKey
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType
import systems.zlink.framework.locations.ZLinkLocationChanged
import systems.zlink.framework.locations.ZLinkLocationChangeType
import systems.zlink.framework.locations.ZLinkLocationKind
import systems.zlink.framework.locations.ZLinkLocationOwnerToken
import systems.zlink.framework.locations.ZLinkLocationPage
import systems.zlink.framework.locations.ZLinkLocationRole
import systems.zlink.framework.locations.ZLinkLocationWatchFilter
import systems.zlink.framework.locations.ZLinkLocationWatchStore
import systems.zlink.framework.locations.ZLinkLocationWriteIntent
import systems.zlink.framework.locations.ZLinkLocationWriteResult
import systems.zlink.framework.locations.ZLinkOwnerLeaseSnapshot
import systems.zlink.framework.locations.ZLinkPageRequest
import systems.zlink.framework.locations.ZLinkPeerLocation
import systems.zlink.framework.locations.ZLinkPeerLocationFilter
import systems.zlink.framework.locations.ZLinkPeerLocationKey
import systems.zlink.framework.locations.ZLinkRouteKind
import systems.zlink.framework.locations.ZLinkRouteLocation
import systems.zlink.framework.locations.ZLinkRouteLocationFilter
import systems.zlink.framework.locations.ZLinkRouteLocationKey
import systems.zlink.framework.locations.ZLinkSpotLocation
import systems.zlink.framework.locations.ZLinkSpotLocationFilter
import systems.zlink.framework.locations.ZLinkSpotLocationKey
import systems.zlink.framework.runtime.locations.ZLinkInMemoryLocationStore
import systems.zlink.framework.spots.ZLinkSpotKind

class KotlinLocationExtensionsTest {
    @Test
    fun `suspend store extensions await CompletionStage without blocking caller code`() = runBlocking {
        val store = ZLinkInMemoryLocationStore()
        val stored = store.updatePeer(peer("owner-a"), ZLinkLocationWriteIntent.NEW_CLAIM)
        val rows = store.listPeers(ZLinkPeerLocationFilter.all())

        assertEquals(1, stored.generation())
        assertEquals(listOf("owner-a"), rows.map { it.ownerId() })
    }

    @Test
    fun `locationPages emits every page item in order`() = runBlocking {
        val pages = mapOf(
            "" to ZLinkLocationPage(listOf("a", "b"), "2"),
            "2" to ZLinkLocationPage(listOf("c"), null),
        )

        val items = locationPages(ZLinkPageRequest(2, null)) { request ->
            java.util.concurrent.CompletableFuture.completedFuture(pages[request.continuationToken() ?: ""])
        }.toList()

        assertEquals(listOf("a", "b", "c"), items)
    }

    @Test
    fun `watch publisher is exposed as Flow`() = runBlocking {
        val event = ZLinkLocationChanged(
            ZLinkLocationKind.PEER,
            "peer-key",
            ZLinkLocationChangeType.UPSERTED,
            3,
            NOW,
        )
        val store = StaticWatchStore(event)

        val events = store.changes(ZLinkLocationWatchFilter(ZLinkLocationKind.PEER, null, null)).toList()

        assertEquals(listOf(event), events)
    }

    @Test
    fun `suspending location store bridge returns CompletionStage to Java callers`() = runBlocking {
        val store = SuspendingStore()
        val result = store.updatePeerAsync(peer("owner-a"), ZLinkLocationWriteIntent.NEW_CLAIM).toCompletableFuture().get()
        val rows = store.listPeersAsync(ZLinkPeerLocationFilter.all()).toCompletableFuture().get()

        assertEquals(42, result.generation())
        assertEquals(listOf("owner-a"), rows.map { it.ownerId() })
    }

    private class StaticWatchStore(
        private val event: ZLinkLocationChanged,
    ) : ZLinkLocationWatchStore {
        override fun watch(filter: ZLinkLocationWatchFilter): Publisher<ZLinkLocationChanged> =
            Publisher { subscriber ->
                subscriber.onSubscribe(object : Subscription {
                    override fun request(n: Long) {
                        subscriber.onNext(event)
                        subscriber.onComplete()
                    }

                    override fun cancel() {
                    }
                })
            }
    }

    private class SuspendingStore : ZLinkSuspendingLocationStore() {
        private val peers = mutableListOf<ZLinkPeerLocation>()

        override suspend fun updatePeer(
            peer: ZLinkPeerLocation,
            intent: ZLinkLocationWriteIntent,
        ): ZLinkLocationWriteResult {
            delay(1)
            peers += peer
            return ZLinkLocationWriteResult.stored(42, NOW)
        }

        override suspend fun removePeer(
            key: ZLinkPeerLocationKey,
            owner: ZLinkLocationOwnerToken,
        ): ZLinkLocationWriteResult = ZLinkLocationWriteResult.stored(owner.generation(), NOW)

        override suspend fun removePeersByOwner(ownerId: String): Long = 0

        override suspend fun listPeers(filter: ZLinkPeerLocationFilter): List<ZLinkPeerLocation> = peers.toList()

        override suspend fun updateSpot(
            spot: ZLinkSpotLocation,
            intent: ZLinkLocationWriteIntent,
        ): ZLinkLocationWriteResult = ZLinkLocationWriteResult.stored(1, NOW)

        override suspend fun removeSpot(
            key: ZLinkSpotLocationKey,
            owner: ZLinkLocationOwnerToken,
        ): ZLinkLocationWriteResult = ZLinkLocationWriteResult.stored(owner.generation(), NOW)

        override suspend fun removeSpotsByOwner(ownerId: String): Long = 0

        override suspend fun resolveSpot(key: ZLinkSpotLocationKey): ZLinkSpotLocation? = null

        override suspend fun listSpots(
            filter: ZLinkSpotLocationFilter,
            page: ZLinkPageRequest,
        ): ZLinkLocationPage<ZLinkSpotLocation> = ZLinkLocationPage(listOf(), null)

        override suspend fun updateActor(
            actor: ZLinkActorLocation,
            intent: ZLinkLocationWriteIntent,
        ): ZLinkLocationWriteResult = ZLinkLocationWriteResult.stored(1, NOW)

        override suspend fun removeActor(
            key: ZLinkActorLocationKey,
            owner: ZLinkLocationOwnerToken,
        ): ZLinkLocationWriteResult = ZLinkLocationWriteResult.stored(owner.generation(), NOW)

        override suspend fun removeActorsByOwner(ownerId: String): Long = 0

        override suspend fun resolveActor(key: ZLinkActorLocationKey): ZLinkActorLocation? = null

        override suspend fun listActors(
            filter: ZLinkActorLocationFilter,
            page: ZLinkPageRequest,
        ): ZLinkLocationPage<ZLinkActorLocation> = ZLinkLocationPage(listOf(), null)

        override suspend fun updateRoute(
            route: ZLinkRouteLocation,
            intent: ZLinkLocationWriteIntent,
        ): ZLinkLocationWriteResult = ZLinkLocationWriteResult.stored(1, NOW)

        override suspend fun removeRoute(
            key: ZLinkRouteLocationKey,
            owner: ZLinkLocationOwnerToken,
        ): ZLinkLocationWriteResult = ZLinkLocationWriteResult.stored(owner.generation(), NOW)

        override suspend fun removeRoutesByOwner(ownerId: String): Long = 0

        override suspend fun resolveRoute(key: ZLinkRouteLocationKey): ZLinkRouteLocation? = null

        override suspend fun listRoutes(
            filter: ZLinkRouteLocationFilter,
            page: ZLinkPageRequest,
        ): ZLinkLocationPage<ZLinkRouteLocation> = ZLinkLocationPage(listOf(), null)

        override suspend fun renewOwnerLease(
            ownerId: String,
            nodeRid: RoutingId,
            leaseTtl: Duration,
        ): ZLinkLocationWriteResult = ZLinkLocationWriteResult.stored(0, NOW)

        override suspend fun removeOwnerLease(ownerId: String): ZLinkLocationWriteResult =
            ZLinkLocationWriteResult.stored(0, NOW)

        override suspend fun listOwnerLeases(): ZLinkOwnerLeaseSnapshot =
            ZLinkOwnerLeaseSnapshot(listOf(), NOW)
    }

    companion object {
        private val NOW: Instant = Instant.parse("2026-07-03T00:00:00Z")
        private val NODE_RID: RoutingId = RoutingId.from(byteArrayOf(0x01))

        private fun peer(ownerId: String): ZLinkPeerLocation =
            ZLinkPeerLocation(
                ZLinkLocationAutoConnectType.ROUTE_MESH,
                "mesh",
                NODE_RID,
                ZLinkLocationRole.ROUTER,
                "tcp://127.0.0.1:6000",
                1,
                0,
                mapOf(),
                listOf(),
                ownerId,
                0,
                NOW,
            )

        @Suppress("unused")
        private fun spot(ownerId: String): ZLinkSpotLocation =
            ZLinkSpotLocation("mesh", NODE_RID, "spot", NODE_RID, ZLinkSpotKind.USER, null, ownerId, 0, NOW)

        @Suppress("unused")
        private fun route(ownerId: String): ZLinkRouteLocation =
            ZLinkRouteLocation(ZLinkRouteKind.ACTOR_SESSION, "route", NODE_RID, ownerId, 0, byteArrayOf(), NOW)
    }
}
