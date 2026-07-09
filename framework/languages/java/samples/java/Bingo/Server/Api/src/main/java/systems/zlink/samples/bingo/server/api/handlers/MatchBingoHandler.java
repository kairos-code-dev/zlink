package systems.zlink.samples.bingo.server.api.handlers;

import systems.zlink.framework.channels.ZLinkClient;
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
    private final ZLinkClient channels;

    public MatchBingoHandler(ZLinkClient channels) {
        this.channels = channels;
    }

    @Override
    public Messages.MatchBingoApiRes handle(
        Messages.MatchBingoApiReq request,
        ZLinkRequestContext context) {
        Messages.AllocateBingoRoomRes allocated = channels.requestToChannel(
                SampleNames.PlayChannel,
                BingoMessages.allocateBingoRoomReq(
                    request.getMode(),
                    request.getActorId(),
                    request.getActorNodeRid()))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.AllocateBingoRoomRes.class);
        return BingoMessages.matchBingoApiRes(allocated.getRoomId(), allocated.getRoomOwnerNodeRid());
    }
}
