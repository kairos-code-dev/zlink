package systems.zlink.samples.kotlin.tictactoe.server.configuration

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.spots.ZLinkSpotKind
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver

class RedisSpotRemoteAddressResolver(
    private val routeStore: RedisRoomRouteStore,
) : ZLinkSpotRemoteAddressResolver {
    override fun resolveSpotRemoteAddressAsync(spotRid: RoutingId): CompletionStage<ZLinkSpotRemoteAddress> {
        val route = routeStore.require(spotRid.toString())
        return CompletableFuture.completedFuture(
            ZLinkSpotRemoteAddress(
                SampleNames.RouteChannel,
                RoutingId.from(route.ownerSpotNodeRid),
                spotRid,
                ZLinkSpotKind.USER,
            ),
        )
    }
}
