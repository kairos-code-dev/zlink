package systems.zlink.samples.tictactoe.server.configuration;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver;

public final class RedisSpotRemoteAddressResolver implements ZLinkSpotRemoteAddressResolver {
    private final RedisRoomRouteStore routes;

    public RedisSpotRemoteAddressResolver(RedisRoomRouteStore routes) {
        this.routes = routes;
    }

    @Override
    public CompletionStage<ZLinkSpotRemoteAddress> resolveSpotRemoteAddressAsync(RoutingId spotRid) {
        RedisRoomRouteStore.RoomRoute route = routes.require(spotRid.toString());
        return CompletableFuture.completedFuture(new ZLinkSpotRemoteAddress(
            SampleNames.RouteChannel,
            RoutingId.from(route.ownerSpotNodeRid()),
            spotRid,
            ZLinkSpotKind.USER));
    }
}
