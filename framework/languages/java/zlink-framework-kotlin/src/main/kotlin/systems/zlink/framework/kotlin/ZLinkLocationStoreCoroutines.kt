@file:JvmName("ZLinkLocationExtensionsKt")
@file:JvmMultifileClass

package systems.zlink.framework.kotlin

import java.time.Duration
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.locations.ActorSpotRefResolver
import systems.zlink.framework.locations.SpotRef
import systems.zlink.framework.locations.SpotRefResolver
import systems.zlink.framework.locations.ZLinkActorLocation
import systems.zlink.framework.locations.ZLinkActorLocationFilter
import systems.zlink.framework.locations.ZLinkActorLocationKey
import systems.zlink.framework.locations.ZLinkActorLocationStore
import systems.zlink.framework.locations.ZLinkLocationOwnerToken
import systems.zlink.framework.locations.ZLinkLocationPage
import systems.zlink.framework.locations.ZLinkLocationRuntimeQuery
import systems.zlink.framework.locations.ZLinkLocationRuntimeStatus
import systems.zlink.framework.locations.ZLinkLocationServiceSummary
import systems.zlink.framework.locations.ZLinkLocationServiceSummaryFilter
import systems.zlink.framework.locations.ZLinkLocationStore
import systems.zlink.framework.locations.ZLinkLocationTopologyEntry
import systems.zlink.framework.locations.ZLinkLocationTopologyFilter
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
