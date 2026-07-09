package systems.zlink.e2e.spotservice.shared;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.spots.SpotRemoteRef;
import systems.zlink.framework.spots.SpotRemoteRefResolver;

public final class SpotRouteResolver implements SpotRemoteRefResolver {
    @Override
    public CompletionStage<SpotRemoteRef> resolveSpotRemoteRefAsync(
        RoutingId spotRid) {
        String rid = spotRid.toString();
        String targetNode = rid.contains("b") ? "play-b" : "play-a";
        return CompletableFuture.completedFuture(new SpotRemoteRef(
            Contracts.ROUTE_CHANNEL,
            RoutingId.from(targetNode),
            spotRid,
            ZLinkSpotKind.USER));
    }
}
