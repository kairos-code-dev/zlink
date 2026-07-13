@file:JvmName("ZLinkLocationExtensionsKt")
@file:JvmMultifileClass

package systems.zlink.framework.kotlin

import java.util.concurrent.CompletionStage
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import systems.zlink.framework.locations.ZLinkActorLocation
import systems.zlink.framework.locations.ZLinkActorLocationFilter
import systems.zlink.framework.locations.ZLinkLocationPage
import systems.zlink.framework.locations.ZLinkLocationRuntimeQuery
import systems.zlink.framework.locations.ZLinkLocationTopologyEntry
import systems.zlink.framework.locations.ZLinkLocationTopologyFilter
import systems.zlink.framework.locations.ZLinkPageRequest
import systems.zlink.framework.locations.ZLinkRouteLocation
import systems.zlink.framework.locations.ZLinkRouteLocationFilter
import systems.zlink.framework.locations.ZLinkSpotLocation
import systems.zlink.framework.locations.ZLinkSpotLocationFilter

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
    locationPages(ZLinkPageRequest(pageSize, null)) { page -> listSpotLocations(filter, page) }

fun ZLinkLocationRuntimeQuery.actors(
    filter: ZLinkActorLocationFilter,
    pageSize: Int,
): Flow<ZLinkActorLocation> =
    locationPages(ZLinkPageRequest(pageSize, null)) { page -> listActorLocations(filter, page) }

fun ZLinkLocationRuntimeQuery.routes(
    filter: ZLinkRouteLocationFilter,
    pageSize: Int,
): Flow<ZLinkRouteLocation> =
    locationPages(ZLinkPageRequest(pageSize, null)) { page -> listRouteLocations(filter, page) }

fun ZLinkLocationRuntimeQuery.topology(
    filter: ZLinkLocationTopologyFilter,
    pageSize: Int,
): Flow<ZLinkLocationTopologyEntry> =
    locationPages(ZLinkPageRequest(pageSize, null)) { page -> listTopology(filter, page) }
