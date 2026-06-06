package systems.zlink.samples.bingo.server.play.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.bingo.shared.contracts.Messages;

@ZLinkHandlerGroup("play")
public final class AllocateBingoRoomHandler
    implements ZLinkRequestHandler<
        Messages.AllocateBingoRoomReq,
        Messages.AllocateBingoRoomRes> {
    private final BingoRoomDirectory rooms;

    public AllocateBingoRoomHandler(BingoRoomDirectory rooms) {
        this.rooms = rooms;
    }

    @Override
    public CompletionStage<Messages.AllocateBingoRoomRes> handleAsync(
        Messages.AllocateBingoRoomReq request,
        ZLinkRequestContext context) {
        return rooms.allocateAsync(request.actorId(), request.mode())
            .thenApply(Messages.AllocateBingoRoomRes::new);
    }
}
