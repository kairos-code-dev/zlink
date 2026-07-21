package systems.zlink.samples.bingo.server.api.handlers;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.spots.SpotHandle;
import systems.zlink.framework.spots.SpotHandleResolver;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.configuration.SampleTimings;
import systems.zlink.samples.bingo.shared.contracts.BingoMessages;
import systems.zlink.samples.bingo.shared.contracts.Messages;

@ZLinkHandlerGroup(SampleNames.ApiChannel)
public final class MatchBingoHandler
    implements ZLinkRequestHandler<
        Messages.MatchBingoApiReq,
        Messages.MatchBingoApiRes> {
    private final ZLinkRouteClient routes;
    private final SpotHandleResolver spots;

    public MatchBingoHandler(ZLinkRouteClient routes, SpotHandleResolver spots) {
        this.routes = routes;
        this.spots = spots;
    }

    @Override
    public java.util.concurrent.CompletionStage<Messages.MatchBingoApiRes> handle(
        Messages.MatchBingoApiReq request,
        ZLinkRequestContext context) {
        RoutingId preferredOwner = RoutingId.from(request.getActorNodeRid());
        return spots.resolveSpotHandle(SampleNames.Mesh, preferredOwner)
            .thenCompose(handle -> routes.requestToSpot(
                    requireSpot(handle, preferredOwner),
                    BingoMessages.allocateBingoRoomReq(
                        request.getMode(),
                        request.getActorId(),
                        request.getActorNodeRid()))
                .timeout(SampleTimings.RequestTimeout)
                .submit(Messages.AllocateBingoRoomRes.class))
            .thenApply(allocated -> BingoMessages.matchBingoApiRes(
                allocated.getRoomId(), allocated.getRoomOwnerNodeRid()));
    }

    private static SpotHandle requireSpot(
        java.util.Optional<SpotHandle> handle,
        RoutingId spotRid) {
        return handle.orElseThrow(() -> new IllegalStateException(
            "spot not found: " + spotRid));
    }
}
