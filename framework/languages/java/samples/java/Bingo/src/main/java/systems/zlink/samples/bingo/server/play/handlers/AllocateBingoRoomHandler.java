package systems.zlink.samples.bingo.server.play.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class AllocateBingoRoomHandler
    implements ZLinkRequestHandler<Messages.AllocateBingoRoomReq, Messages.AllocateBingoRoomRes> {
    @Override
    public CompletionStage<Messages.AllocateBingoRoomRes> handleAsync(
        Messages.AllocateBingoRoomReq request,
        ZLinkRequestContext context) {
        return CompletableFuture.completedFuture(new Messages.AllocateBingoRoomRes("room-1"));
    }
}
