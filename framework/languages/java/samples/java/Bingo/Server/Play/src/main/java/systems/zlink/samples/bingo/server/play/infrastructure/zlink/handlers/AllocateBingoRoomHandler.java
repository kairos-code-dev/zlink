package systems.zlink.samples.bingo.server.play.infrastructure.zlink.handlers;


import com.fasterxml.jackson.databind.ObjectMapper;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.bingo.server.configuration.SampleTopology;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.BingoRoomSpot;
import systems.zlink.samples.bingo.server.play.application.roomallocation.BingoRoomAllocation;
import systems.zlink.samples.bingo.server.play.application.roomallocation.BingoRoomAllocator;
import systems.zlink.samples.bingo.server.play.domain.bingo.BingoRoomModels;
import systems.zlink.samples.bingo.shared.contracts.BingoMessages;
import systems.zlink.samples.bingo.shared.contracts.Messages;

@ZLinkHandlerGroup("play-route")
public final class AllocateBingoRoomHandler
    implements ZLinkRequestHandler<
        Messages.AllocateBingoRoomReq,
        Messages.AllocateBingoRoomRes> {
    private final BingoRoomAllocator rooms;
    private final ZLinkSpotManager spots;
    private final ObjectMapper json;

    public AllocateBingoRoomHandler(
        BingoRoomAllocator rooms,
        ZLinkSpotManager spots,
        ObjectMapper json) {
        this.rooms = rooms;
        this.spots = spots;
        this.json = json;
    }

    @Override
    public java.util.concurrent.CompletionStage<Messages.AllocateBingoRoomRes> handle(
        Messages.AllocateBingoRoomReq request,
        ZLinkRequestContext context) {
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
        if (!allocation.ownerPlayNodeRid().equals(SampleTopology.selectedPlayNodeRid())) {
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        }

        return spots.getOrCreate(
            BingoRoomSpot.class,
            RoutingId.from(allocation.roomId()),
            ZLinkMessage.of(allocation.settings())).thenApply(ignored -> null);
    }
}
