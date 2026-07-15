package systems.zlink.samples.bingo.server.api.handlers;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.configuration.SampleTimings;
import systems.zlink.samples.bingo.shared.contracts.BingoMessages;
import systems.zlink.samples.bingo.shared.contracts.Messages;

@ZLinkHandlerGroup("api")
public final class MatchBingoHandler
    implements ZLinkRequestHandler<
        Messages.MatchBingoApiReq,
        Messages.MatchBingoApiRes> {
    private final ZLinkRouteClient routes;

    public MatchBingoHandler(ZLinkRouteClient routes) {
        this.routes = routes;
    }

    @Override
    public java.util.concurrent.CompletionStage<Messages.MatchBingoApiRes> handle(
        Messages.MatchBingoApiReq request,
        ZLinkRequestContext context) {
        return routes.requestToNode(
                SampleNames.PlayChannel,
                RoutingId.from(request.getActorNodeRid()),
                BingoMessages.allocateBingoRoomReq(
                    request.getMode(),
                    request.getActorId(),
                    request.getActorNodeRid()))
            .timeout(SampleTimings.RequestTimeout)
            .submit(Messages.AllocateBingoRoomRes.class)
            .thenApply(allocated -> BingoMessages.matchBingoApiRes(
                allocated.getRoomId(), allocated.getRoomOwnerNodeRid()));
    }
}
