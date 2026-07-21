package systems.zlink.samples.gamequest.server.questmission.spots;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.SpotHandle;
import systems.zlink.framework.spots.SpotHandleResolver;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.gamequest.server.configuration.SampleNames;
import systems.zlink.samples.gamequest.server.configuration.SampleTimings;

public final class PlayerQuestRouter {
    private final ZLinkSpotManager spots;
    private final ZLinkRouteClient routes;
    private final SpotHandleResolver handles;

    public PlayerQuestRouter(ZLinkSpotManager spots, ZLinkRouteClient routes, SpotHandleResolver handles) {
        this.spots = spots;
        this.routes = routes;
        this.handles = handles;
    }

    public <T> CompletionStage<T> request(String playerId, Object request, Class<T> responseType) {
        return ownerHandle(playerId)
            .thenCompose(handle -> request(handle, request, responseType));
    }

    public CompletionStage<Void> send(String playerId, Object message) {
        return ownerHandle(playerId)
            .thenApply(handle -> {
                routes.sendToSpot(handle, message).submit();
                return null;
            });
    }

    private CompletionStage<SpotHandle> ownerHandle(String playerId) {
        RoutingId spotRid = RoutingId.from(playerId);
        return spots.getOrCreate(
                PlayerQuestSpot.class,
                spotRid,
                ZLinkMessage.of(new PlayerQuestCreateReq(playerId)))
            .thenCompose(ignored -> handles.resolveSpotHandle(spotRid))
            .thenApply(found -> found.orElseThrow(() ->
                new IllegalStateException("player quest Spot is not registered: " + spotRid)));
    }

    private <T> CompletionStage<T> request(SpotHandle handle, Object request, Class<T> responseType) {
        return routes.requestToSpot(handle, request)
            .timeout(SampleTimings.RequestTimeout)
            .submit(responseType);
    }
}
