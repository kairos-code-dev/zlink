package systems.zlink.samples.bingo.server.play.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.samples.bingo.shared.contracts.Messages;

@ZLinkHandlerGroup("play")
public final class AllocateBingoRoomHandler {
    @ZLinkRequest(packetName = "AllocateBingoRoom")
    public CompletionStage<Messages.AllocateBingoRoomRes> handleAsync(
        Messages.AllocateBingoRoomReq request) {
        return CompletableFuture.completedFuture(new Messages.AllocateBingoRoomRes("room-1"));
    }
}
