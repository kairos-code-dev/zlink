package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.handlers

import systems.zlink.framework.kotlin.await
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.ZLinkMessageContext
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.server.play.application.roomallocation.BingoRoomAllocation
import systems.zlink.samples.kotlin.bingo.server.play.application.roomallocation.BingoRoomAllocator
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomRes
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTimings

@ZLinkHandlerGroup(SampleNames.RoomSpotDiscovery)
class AllocateBingoRoomHandler(
    private val rooms: BingoRoomAllocator,
    private val spots: ZLinkSpotManager,
) : ZLinkSuspendingRequestHandler<AllocateBingoRoomReq, AllocateBingoRoomRes> {
    override suspend fun handle(
        request: AllocateBingoRoomReq,
        context: ZLinkMessageContext,
    ) = run {
        val allocation = rooms.allocate(
            request.actorId,
            request.mode,
        )
        spots.getOrCreate(allocation.roomId, BingoRoomSpot::class.java.name)
            .inMesh(SampleNames.Mesh)
            .request(allocation.settings)
            .timeout(SampleTimings.RequestTimeout)
            .submit()
            .await()
        AllocateBingoRoomRes(allocation.roomId)
    }
}
