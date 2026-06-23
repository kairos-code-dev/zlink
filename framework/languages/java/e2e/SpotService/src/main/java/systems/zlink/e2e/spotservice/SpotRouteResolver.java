package systems.zlink.e2e.spotservice;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver;

public final class SpotRouteResolver implements ZLinkSpotRemoteAddressResolver {
    @Override
    public CompletionStage<ZLinkSpotRemoteAddress> resolveSpotRemoteAddressAsync(
        RoutingId spotRid) {
        String rid = spotRid.toString();
        String targetNode = rid.contains("b") ? "play-b" : "play-a";
        return CompletableFuture.completedFuture(new ZLinkSpotRemoteAddress(
            Contracts.INGRESS_CHANNEL,
            RoutingId.from(targetNode),
            spotRid,
            ZLinkSpotKind.USER));
    }
}
