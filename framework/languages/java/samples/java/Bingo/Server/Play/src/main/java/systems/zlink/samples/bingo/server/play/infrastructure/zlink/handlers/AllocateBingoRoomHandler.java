package systems.zlink.samples.bingo.server.play.infrastructure.zlink.handlers;


import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.ZLinkSpotRequestHandler;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.entryspot.BingoEntrySpot;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.BingoRoomSpot;
import systems.zlink.samples.bingo.server.play.application.roomallocation.BingoRoomAllocation;
import systems.zlink.samples.bingo.server.play.application.roomallocation.BingoRoomAllocator;
import systems.zlink.samples.bingo.shared.contracts.BingoMessages;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class AllocateBingoRoomHandler
    implements ZLinkSpotRequestHandler<BingoEntrySpot,
        Messages.AllocateBingoRoomReq,
        Messages.AllocateBingoRoomRes> {
    private final BingoRoomAllocator rooms;
    private final ZLinkSpotManager spots;

    public AllocateBingoRoomHandler(
        BingoRoomAllocator rooms,
        ZLinkSpotManager spots) {
        this.rooms = rooms;
        this.spots = spots;
    }

    @Override
    public java.util.concurrent.CompletionStage<Messages.AllocateBingoRoomRes> handle(
        BingoEntrySpot spot,
        Messages.AllocateBingoRoomReq request) {
        String playNodeRid = spot.context().nodeRid().toString();
        BingoRoomAllocation allocation = rooms.allocate(
            request.getActorId(),
            request.getMode(),
            request.getPreferredOwnerNodeRid());
        return ensureLocalRoom(allocation, playNodeRid).thenApply(ignored ->
            BingoMessages.allocateBingoRoomRes(
                allocation.roomId(),
                allocation.ownerPlayNodeRid()));
    }

    private java.util.concurrent.CompletionStage<Void> ensureLocalRoom(
        BingoRoomAllocation allocation,
        String playNodeRid) {
        if (!allocation.ownerPlayNodeRid().equals(playNodeRid)) {
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        }

        return spots.getOrCreate(
            BingoRoomSpot.class,
            RoutingId.from(allocation.roomId()),
            ZLinkMessage.of(allocation.settings())).thenApply(ignored -> null);
    }
}
