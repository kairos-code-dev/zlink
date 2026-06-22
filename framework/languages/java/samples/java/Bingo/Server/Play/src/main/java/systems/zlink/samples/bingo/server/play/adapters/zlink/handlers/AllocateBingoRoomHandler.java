package systems.zlink.samples.bingo.server.play.adapters.zlink.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.bingo.server.configuration.SampleTopology;
import systems.zlink.samples.bingo.server.play.adapters.zlink.spots.BingoRoomSpot;
import systems.zlink.samples.bingo.server.play.application.roomallocation.BingoRoomAllocation;
import systems.zlink.samples.bingo.server.play.application.roomallocation.BingoRoomAllocator;
import systems.zlink.samples.bingo.server.play.domain.bingo.BingoRoomModels;
import systems.zlink.samples.bingo.shared.contracts.Messages;

@ZLinkHandlerGroup("play-route")
public final class AllocateBingoRoomHandler
    implements ZLinkRouteRequestHandler<
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
        ZLinkRouteRequestContext context) {
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
            System.out.println(
                "bingo room allocation: remote owner. room=" + allocation.roomId()
                    + ", owner=" + allocation.ownerPlayNodeRid()
                    + ", local=" + SampleTopology.selectedPlayNodeRid());
            return;
        }

        Message settingsPart = serialize(allocation.settings());
        try {
            var created = await(spots.getOrCreate(BingoRoomSpot.class, RoutingId.from(allocation.roomId()), settingsPart));
            System.out.println(
                "bingo room allocation: local owner. room=" + allocation.roomId()
                    + ", owner=" + allocation.ownerPlayNodeRid()
                    + ", local=" + SampleTopology.selectedPlayNodeRid()
                    + ", state=" + created.state());
        } finally {
            settingsPart.close();
        }
    }

    private Message serialize(BingoRoomModels.BingoRoomSettings settings) {
        try {
            return Message.from(json.writeValueAsBytes(settings));
        } catch (JsonProcessingException ex) {
            throw new IllegalStateException("Failed to encode bingo room settings.", ex);
        }
    }
}
