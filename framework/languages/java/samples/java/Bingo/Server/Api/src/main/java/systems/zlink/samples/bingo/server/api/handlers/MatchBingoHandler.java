package systems.zlink.samples.bingo.server.api.handlers;

import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.configuration.SampleTimings;
import systems.zlink.samples.bingo.shared.contracts.Messages;

@ZLinkHandlerGroup("api")
public final class MatchBingoHandler
    implements ZLinkRequestHandler<
        Messages.MatchBingoApiReq,
        Messages.MatchBingoApiRes> {
    private final ZLinkClient client;

    public MatchBingoHandler(ZLinkClient client) {
        this.client = client;
    }

    @Override
    public Messages.MatchBingoApiRes handle(
        Messages.MatchBingoApiReq request,
        ZLinkRequestContext context) {
        Messages.AllocateBingoRoomRes allocated = client.requestToChannel(
                SampleNames.PlayChannel,
                new Messages.AllocateBingoRoomReq(request.actorId(), request.mode()))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.AllocateBingoRoomRes.class);
        return new Messages.MatchBingoApiRes(allocated.roomId());
    }
}
