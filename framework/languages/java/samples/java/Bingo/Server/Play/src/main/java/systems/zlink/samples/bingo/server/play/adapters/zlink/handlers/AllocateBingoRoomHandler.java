package systems.zlink.samples.bingo.server.play.adapters.zlink.handlers;

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
    public Messages.AllocateBingoRoomRes handle(
        Messages.AllocateBingoRoomReq request,
        ZLinkRequestContext context) {
        return new Messages.AllocateBingoRoomRes(
            rooms.allocate(request.actorId(), request.mode()));
    }
}
