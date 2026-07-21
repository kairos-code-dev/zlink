package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.handlers

import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.kotlin.await
import systems.zlink.framework.kotlin.ZLinkSuspendingSpotRequestHandler
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.entryspot.BingoEntrySpot
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.server.play.application.roomallocation.BingoRoomAllocation
import systems.zlink.samples.kotlin.bingo.server.play.application.roomallocation.BingoRoomAllocator
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomRes

class AllocateBingoRoomHandler(
    private val rooms: BingoRoomAllocator,
    private val spots: ZLinkSpotManager,
) : ZLinkSuspendingSpotRequestHandler<BingoEntrySpot, AllocateBingoRoomReq, AllocateBingoRoomRes> {
    override suspend fun handle(
        spot: BingoEntrySpot,
        request: AllocateBingoRoomReq,
    ) = run {
        val allocation = rooms.allocate(
            request.actorId,
            request.mode,
            request.preferredOwnerNodeRid,
        )
        ensureLocalRoom(allocation, spot.context().nodeRid().toString())
        AllocateBingoRoomRes(allocation.roomId, allocation.ownerPlayNodeRid)
    }

    private suspend fun ensureLocalRoom(allocation: BingoRoomAllocation, playNodeRid: String) {
        if (allocation.ownerPlayNodeRid != playNodeRid) {
            return
        }

        spots.getOrCreate(BingoRoomSpot::class.java, RoutingId.from(allocation.roomId), ZLinkMessage.of(allocation.settings))
            .await()
    }
}
