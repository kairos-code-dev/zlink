package systems.zlink.samples.bingo.server.api.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.bingo.shared.configuration.SampleNames;
import systems.zlink.samples.bingo.shared.configuration.SampleTimings;
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
    public CompletionStage<Messages.MatchBingoApiRes> handleAsync(
        Messages.MatchBingoApiReq request,
        ZLinkRequestContext context) {
        return client.requestToChannel(
                SampleNames.PlayChannel,
                new Messages.AllocateBingoRoomReq(request.actorId(), request.mode()))
            .packetName("AllocateBingoRoomReq")
            .timeout(SampleTimings.RequestTimeout)
            .submitAsync(Messages.AllocateBingoRoomRes.class)
            .thenApply(allocated -> new Messages.MatchBingoApiRes(allocated.roomId()));
    }
}
