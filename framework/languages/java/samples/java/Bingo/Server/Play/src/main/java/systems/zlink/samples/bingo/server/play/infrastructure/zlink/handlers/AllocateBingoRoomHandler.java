package systems.zlink.samples.bingo.server.play.infrastructure.zlink.handlers;


import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.bingo.server.configuration.SampleTopology;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.BingoRoomSpot;
import systems.zlink.samples.bingo.server.play.application.roomallocation.BingoRoomAllocation;
import systems.zlink.samples.bingo.server.play.application.roomallocation.BingoRoomAllocator;
import systems.zlink.samples.bingo.server.play.domain.bingo.BingoRoomModels;
import systems.zlink.samples.bingo.shared.contracts.BingoMessages;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class AllocateBingoRoomHandler
    implements ZLinkRouteRequestHandler<
        Messages.AllocateBingoRoomReq,
        Messages.AllocateBingoRoomRes> {
    private final BingoRoomAllocator rooms;
    private final ZLinkSpotManager spots;
    private final String playNodeRid;

    public AllocateBingoRoomHandler(
        BingoRoomAllocator rooms,
        ZLinkSpotManager spots,
        SampleTopology topology) {
        this.rooms = rooms;
        this.spots = spots;
        this.playNodeRid = topology.selectedPlayNodeRid();
    }

    @Override
    public java.util.concurrent.CompletionStage<Messages.AllocateBingoRoomRes> handle(
        Messages.AllocateBingoRoomReq request,
        ZLinkRouteRequestContext context) {
        BingoRoomAllocation allocation = rooms.allocate(
            request.getActorId(),
            request.getMode(),
            request.getPreferredOwnerNodeRid());
        return ensureLocalRoom(allocation).thenApply(ignored ->
            BingoMessages.allocateBingoRoomRes(
                allocation.roomId(),
                allocation.ownerPlayNodeRid()));
    }

    private java.util.concurrent.CompletionStage<Void> ensureLocalRoom(BingoRoomAllocation allocation) {
        if (!allocation.ownerPlayNodeRid().equals(playNodeRid)) {
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        }

        return spots.getOrCreate(
            BingoRoomSpot.class,
            RoutingId.from(allocation.roomId()),
            ZLinkMessage.of(allocation.settings())).thenApply(ignored -> null);
    }
}
