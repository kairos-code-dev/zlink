package systems.zlink.samples.bingo.server.play.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.samples.bingo.shared.contracts.Messages;

@ZLinkHandlerGroup("play")
public final class AllocateBingoRoomHandler {
    private final BingoRoomDirectory rooms;

    public AllocateBingoRoomHandler(BingoRoomDirectory rooms) {
        this.rooms = rooms;
    }

    @ZLinkRequest(packetName = "AllocateBingoRoomReq")
    public CompletionStage<Messages.AllocateBingoRoomRes> handleAsync(
        Messages.AllocateBingoRoomReq request) {
        return rooms.allocateAsync(request.actorId(), request.mode())
            .thenApply(Messages.AllocateBingoRoomRes::new);
    }
}
