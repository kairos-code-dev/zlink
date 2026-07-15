package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.handlers

import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.await
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.server.play.application.roomallocation.BingoRoomAllocation
import systems.zlink.samples.kotlin.bingo.server.play.application.roomallocation.BingoRoomAllocator
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomRes

@ZLinkHandlerGroup("play-route")
class AllocateBingoRoomHandler(
    private val rooms: BingoRoomAllocator,
    private val spots: ZLinkSpotManager,
    private val topology: SampleTopology,
) : ZLinkSuspendingRequestHandler<AllocateBingoRoomReq, AllocateBingoRoomRes> {
    override suspend fun handle(
        request: AllocateBingoRoomReq,
        context: ZLinkRequestContext,
    ) = run {
        val allocation = rooms.allocate(
            request.actorId,
            request.mode,
            request.preferredOwnerNodeRid,
        )
        ensureLocalRoom(allocation)
        AllocateBingoRoomRes(allocation.roomId, allocation.ownerPlayNodeRid)
    }

    private suspend fun ensureLocalRoom(allocation: BingoRoomAllocation) {
        if (allocation.ownerPlayNodeRid != topology.selectedPlayNodeRid()) {
            return
        }

        spots.getOrCreate(BingoRoomSpot::class.java, RoutingId.from(allocation.roomId), ZLinkMessage.of(allocation.settings))
            .await()
    }
}
