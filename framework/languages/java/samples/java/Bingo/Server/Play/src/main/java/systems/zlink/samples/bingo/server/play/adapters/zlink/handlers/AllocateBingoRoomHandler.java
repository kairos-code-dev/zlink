package systems.zlink.samples.bingo.server.play.adapters.zlink.handlers;

import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.bingo.shared.contracts.Messages;

@ZLinkHandlerGroup("play-route")
public final class AllocateBingoRoomHandler
    implements ZLinkRouteRequestHandler<
        Messages.AllocateBingoRoomReq,
        Messages.AllocateBingoRoomRes> {
    private final BingoRoomDirectory rooms;

    public AllocateBingoRoomHandler(BingoRoomDirectory rooms) {
        this.rooms = rooms;
    }

    @Override
    public Messages.AllocateBingoRoomRes handle(
        Messages.AllocateBingoRoomReq request,
        ZLinkRouteRequestContext context) {
        BingoMatchReservation reservation = rooms.allocate(
            request.actorId(),
            request.mode(),
            request.preferredOwnerNodeRid());
        return new Messages.AllocateBingoRoomRes(
            reservation.roomId(),
            reservation.ownerPlayNodeRid());
    }
}
