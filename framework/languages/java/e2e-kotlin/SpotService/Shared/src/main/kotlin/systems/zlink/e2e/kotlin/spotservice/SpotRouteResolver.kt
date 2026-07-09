package systems.zlink.e2e.kotlin.spotservice

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.spots.ZLinkSpotKind
import systems.zlink.framework.spots.SpotRemoteRef
import systems.zlink.framework.spots.SpotRemoteRefResolver

class SpotRouteResolver : SpotRemoteRefResolver {
    override fun resolveSpotRemoteRefAsync(spotRid: RoutingId): CompletionStage<SpotRemoteRef> {
        val targetNode = if (spotRid.toString().contains("b")) "play-b" else "play-a"
        return CompletableFuture.completedFuture(
            SpotRemoteRef(
                Contracts.ROUTE_CHANNEL,
                RoutingId.from(targetNode),
                spotRid,
                ZLinkSpotKind.USER,
            ),
        )
    }
}
