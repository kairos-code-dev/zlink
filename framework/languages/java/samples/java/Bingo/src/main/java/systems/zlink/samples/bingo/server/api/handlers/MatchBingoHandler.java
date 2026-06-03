package systems.zlink.samples.bingo.server.api.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class MatchBingoHandler
    implements ZLinkRequestHandler<Messages.MatchBingoApiReq, Messages.MatchBingoApiRes> {
    @Override
    public CompletionStage<Messages.MatchBingoApiRes> handleAsync(
        Messages.MatchBingoApiReq request,
        ZLinkRequestContext context) {
        return CompletableFuture.completedFuture(new Messages.MatchBingoApiRes("room-1"));
    }
}
