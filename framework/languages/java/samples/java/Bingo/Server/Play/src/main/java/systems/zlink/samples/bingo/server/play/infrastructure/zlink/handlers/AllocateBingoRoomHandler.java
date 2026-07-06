package systems.zlink.samples.bingo.server.play.infrastructure.zlink.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

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
import systems.zlink.samples.bingo.shared.contracts.Messages;

@ZLinkHandlerGroup("play")
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
    public Messages.AllocateBingoRoomRes handle(
        Messages.AllocateBingoRoomReq request,
        ZLinkRequestContext context) {
        BingoRoomAllocation allocation = rooms.allocate(
            request.actorId(),
            request.mode(),
            request.preferredOwnerNodeRid());
        ensureLocalRoom(allocation);
        return new Messages.AllocateBingoRoomRes(
            allocation.roomId(),
            allocation.ownerPlayNodeRid());
    }

    private void ensureLocalRoom(BingoRoomAllocation allocation) {
        if (!allocation.ownerPlayNodeRid().equals(SampleTopology.selectedPlayNodeRid())) {
            return;
        }

        await(spots.getOrCreate(
            BingoRoomSpot.class,
            RoutingId.from(allocation.roomId()),
            ZLinkMessage.of(allocation.settings())));
    }
}
