@file:JvmName("ZLinkLocationExtensionsKt")
@file:JvmMultifileClass

package systems.zlink.framework.kotlin

import java.time.Duration
import systems.zlink.contracts.core.RoutingId
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
import systems.zlink.framework.locations.ZLinkLocationWriteStatus
import systems.zlink.framework.locations.ZLinkMeshNodeDescriptor
import systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey
import systems.zlink.framework.locations.ZLinkMeshNodeLocationStore
import systems.zlink.framework.locations.ZLinkOwnerLeaseClaimResult
import systems.zlink.framework.locations.ZLinkOwnerLeaseReadResult
import systems.zlink.framework.locations.ZLinkOwnerLeaseReleaseResult
import systems.zlink.framework.locations.ZLinkOwnerLeaseRenewResult
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
import systems.zlink.framework.spots.ActorSpotHandleResolver
import systems.zlink.framework.spots.SpotHandle
import systems.zlink.framework.spots.SpotHandleResolver

suspend fun ZLinkMeshNodeLocationStore.updateMeshNode(
    descriptor: ZLinkMeshNodeDescriptor,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult =
    awaitFrameworkStage(this.updateMeshNode(descriptor, intent))

suspend fun ZLinkMeshNodeLocationStore.removeMeshNode(
    key: ZLinkMeshNodeDescriptorKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteStatus =
    awaitFrameworkStage(this.removeMeshNode(key, owner))

suspend fun ZLinkMeshNodeLocationStore.listMeshNodes(
    meshName: String,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkMeshNodeDescriptor> =
    awaitFrameworkStage(this.listMeshNodes(meshName, page))

suspend fun ZLinkPeerLocationStore.updatePeer(
    peer: ZLinkPeerLocation,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult =
    awaitFrameworkStage(this.updatePeer(peer, intent))

suspend fun ZLinkPeerLocationStore.removePeer(
    key: ZLinkPeerLocationKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteResult =
    awaitFrameworkStage(this.removePeer(key, owner))

suspend fun ZLinkPeerLocationStore.listPeerLocations(filter: ZLinkPeerLocationFilter): List<ZLinkPeerLocation> =
    awaitFrameworkStage(this.listPeerLocations(filter))

suspend fun ZLinkSpotLocationStore.updateSpot(
    spot: ZLinkSpotLocation,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult =
    awaitFrameworkStage(this.updateSpot(spot, intent))

suspend fun ZLinkSpotLocationStore.removeSpot(
    key: ZLinkSpotLocationKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteResult =
    awaitFrameworkStage(this.removeSpot(key, owner))

suspend fun ZLinkSpotLocationStore.resolveSpot(key: ZLinkSpotLocationKey): ZLinkSpotLocation? =
    awaitFrameworkStage(this.resolveSpot(key))

suspend fun ZLinkSpotLocationStore.listSpotLocations(
    filter: ZLinkSpotLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkSpotLocation> =
    awaitFrameworkStage(this.listSpotLocations(filter, page))

suspend fun ZLinkActorLocationStore.updateActor(
    actor: ZLinkActorLocation,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult =
    awaitFrameworkStage(this.updateActor(actor, intent))

suspend fun ZLinkActorLocationStore.removeActor(
    key: ZLinkActorLocationKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteResult =
    awaitFrameworkStage(this.removeActor(key, owner))

suspend fun ZLinkActorLocationStore.resolveActor(key: ZLinkActorLocationKey): ZLinkActorLocation? =
    awaitFrameworkStage(this.resolveActor(key))

suspend fun ZLinkActorLocationStore.listActorLocations(
    filter: ZLinkActorLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkActorLocation> =
    awaitFrameworkStage(this.listActorLocations(filter, page))

suspend fun ZLinkRouteLocationStore.updateRoute(
    route: ZLinkRouteLocation,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult =
    awaitFrameworkStage(this.updateRoute(route, intent))

suspend fun ZLinkRouteLocationStore.removeRoute(
    key: ZLinkRouteLocationKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteResult =
    awaitFrameworkStage(this.removeRoute(key, owner))

suspend fun ZLinkRouteLocationStore.resolveRoute(key: ZLinkRouteLocationKey): ZLinkRouteLocation? =
    awaitFrameworkStage(this.resolveRoute(key))

suspend fun ZLinkRouteLocationStore.listRouteLocations(
    filter: ZLinkRouteLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkRouteLocation> =
    awaitFrameworkStage(this.listRouteLocations(filter, page))

suspend fun ZLinkLocationStore.claimOwnerLease(
    ownerId: String,
    leaseTtl: Duration,
): ZLinkOwnerLeaseClaimResult =
    awaitFrameworkStage(this.claimOwnerLease(ownerId, leaseTtl))

suspend fun ZLinkLocationStore.readOwnerLease(
    ownerId: String,
): ZLinkOwnerLeaseReadResult =
    awaitFrameworkStage(this.readOwnerLease(ownerId))

suspend fun ZLinkLocationStore.renewOwnerLease(
    token: ZLinkLocationOwnerToken,
    leaseTtl: Duration,
): ZLinkOwnerLeaseRenewResult =
    awaitFrameworkStage(this.renewOwnerLease(token, leaseTtl))

suspend fun ZLinkLocationStore.releaseOwnerLease(
    token: ZLinkLocationOwnerToken,
): ZLinkOwnerLeaseReleaseResult =
    awaitFrameworkStage(this.releaseOwnerLease(token))

suspend fun ZLinkLocationStore.removeAllByOwner(owner: ZLinkLocationOwnerToken): Long =
    awaitFrameworkStage(this.removeAllByOwner(owner))

suspend fun ZLinkPeerLocationResolver.listLivePeers(filter: ZLinkPeerLocationFilter): List<ZLinkPeerLocation> =
    awaitFrameworkStage(this.listLivePeers(filter))

suspend fun SpotHandleResolver.resolveSpotHandle(spotRid: RoutingId): SpotHandle? =
    awaitFrameworkStage(this.resolveSpotHandle(spotRid)).orElse(null)

suspend fun ActorSpotHandleResolver.resolveActorSpotHandle(actorId: String): SpotHandle? =
    awaitFrameworkStage(this.resolveActorSpotHandle(actorId)).orElse(null)

suspend fun ZLinkLocationRuntimeQuery.status(): ZLinkLocationRuntimeStatus =
    awaitFrameworkStage(getStatus())

suspend fun ZLinkLocationRuntimeQuery.listPeerLocations(filter: ZLinkPeerLocationFilter): List<ZLinkPeerLocation> =
    awaitFrameworkStage(this.listPeerLocations(filter))

suspend fun ZLinkLocationRuntimeQuery.listSpotLocations(
    filter: ZLinkSpotLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkSpotLocation> =
    awaitFrameworkStage(this.listSpotLocations(filter, page))

suspend fun ZLinkLocationRuntimeQuery.listActorLocations(
    filter: ZLinkActorLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkActorLocation> =
    awaitFrameworkStage(this.listActorLocations(filter, page))

suspend fun ZLinkLocationRuntimeQuery.listRouteLocations(
    filter: ZLinkRouteLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkRouteLocation> =
    awaitFrameworkStage(this.listRouteLocations(filter, page))

suspend fun ZLinkLocationRuntimeQuery.listTopology(
    filter: ZLinkLocationTopologyFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkLocationTopologyEntry> =
    awaitFrameworkStage(this.listTopology(filter, page))

suspend fun ZLinkLocationRuntimeQuery.listServiceSummaries(
    filter: ZLinkLocationServiceSummaryFilter,
): List<ZLinkLocationServiceSummary> =
    awaitFrameworkStage(this.listServiceSummaries(filter))
