package systems.zlink.samples.bingo.server.api.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.samples.bingo.shared.contracts.Messages;

@ZLinkHandlerGroup("api")
public final class MatchBingoHandler {
    @ZLinkRequest(packetName = "MatchBingo")
    public CompletionStage<Messages.MatchBingoApiRes> handleAsync(
        Messages.MatchBingoApiReq request) {
        return CompletableFuture.completedFuture(new Messages.MatchBingoApiRes("room-1"));
    }
}
