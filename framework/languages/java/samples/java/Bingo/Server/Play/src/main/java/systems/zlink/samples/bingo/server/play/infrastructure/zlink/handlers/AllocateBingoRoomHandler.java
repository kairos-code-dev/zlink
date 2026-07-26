package systems.zlink.samples.bingo.server.play.infrastructure.zlink.handlers;


import systems.zlink.framework.ZLinkMessageContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.bingo.server.play.application.roomallocation.BingoRoomAllocation;
import systems.zlink.samples.bingo.server.play.application.roomallocation.BingoRoomAllocator;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.BingoRoomSpot;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.configuration.SampleTimings;
import systems.zlink.samples.bingo.shared.contracts.BingoMessages;
import systems.zlink.samples.bingo.shared.contracts.Messages;

@ZLinkHandlerGroup(SampleNames.RoomSpotDiscovery)
public final class AllocateBingoRoomHandler
    implements ZLinkRequestHandler<Messages.AllocateBingoRoomReq, Messages.AllocateBingoRoomRes> {
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
        Messages.AllocateBingoRoomReq request,
        ZLinkMessageContext context) {
        BingoRoomAllocation allocation = rooms.allocate(
            request.getActorId(),
            request.getMode());
        return spots.getOrCreate(allocation.roomId(), BingoRoomSpot.class.getName())
            .inMesh(SampleNames.Mesh)
            .request(allocation.settings())
            .timeout(SampleTimings.RequestTimeout)
            .submit()
            .thenApply(ignored -> BingoMessages.allocateBingoRoomRes(allocation.roomId()));
    }
}
