package systems.zlink.e2e.kotlin.spotservice

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.spots.ZLinkSpotKind
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver

class SpotRouteResolver : ZLinkSpotRemoteAddressResolver {
    override fun resolveSpotRemoteAddressAsync(spotRid: RoutingId): CompletionStage<ZLinkSpotRemoteAddress> {
        val targetNode = if (spotRid.toString().contains("b")) "play-b" else "play-a"
        return CompletableFuture.completedFuture(
            ZLinkSpotRemoteAddress(
                Contracts.ROUTE_CHANNEL,
                RoutingId.from(targetNode),
                spotRid,
                ZLinkSpotKind.USER,
            ),
        )
    }
}
