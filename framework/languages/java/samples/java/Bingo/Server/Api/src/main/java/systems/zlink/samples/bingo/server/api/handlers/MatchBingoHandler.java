package systems.zlink.samples.bingo.server.api.handlers;

import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.ZLinkMessageContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
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

    public MatchBingoHandler(ZLinkRouteClient routes) {
        this.routes = routes;
    }

    @Override
    public java.util.concurrent.CompletionStage<Messages.MatchBingoApiRes> handle(
        Messages.MatchBingoApiReq request,
        ZLinkMessageContext context) {
        return routes.requestToChannel(
                SampleNames.RoomSpotDiscovery,
                BingoMessages.allocateBingoRoomReq(
                    request.getMode(),
                    request.getActorId()))
            .timeout(SampleTimings.RequestTimeout)
            .submit(Messages.AllocateBingoRoomRes.class)
            .thenApply(allocated -> BingoMessages.matchBingoApiRes(allocated.getRoomId()));
    }
}
