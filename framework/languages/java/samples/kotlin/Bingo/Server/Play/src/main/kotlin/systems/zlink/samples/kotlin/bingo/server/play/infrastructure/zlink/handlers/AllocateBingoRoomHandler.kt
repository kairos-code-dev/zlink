package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.handlers

import com.fasterxml.jackson.databind.ObjectMapper
import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.channels.ZLinkRouteRequestContext
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkSuspendingRouteRequestHandler
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.server.play.application.roomallocation.BingoRoomAllocation
import systems.zlink.samples.kotlin.bingo.server.play.application.roomallocation.BingoRoomAllocator
import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomSettings
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomRes

@ZLinkHandlerGroup("play-route")
class AllocateBingoRoomHandler(
    private val rooms: BingoRoomAllocator,
    private val spots: ZLinkSpotManager,
    private val json: ObjectMapper,
) : ZLinkSuspendingRouteRequestHandler<AllocateBingoRoomReq, AllocateBingoRoomRes> {
    override suspend fun handle(
        request: AllocateBingoRoomReq,
        context: ZLinkRouteRequestContext,
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
        if (allocation.ownerPlayNodeRid != SampleTopology.selectedPlayNodeRid()) {
            return
        }

        spots.getOrCreate(BingoRoomSpot::class.java, RoutingId.from(allocation.roomId), allocation.settings)
            .await()
    }
}
