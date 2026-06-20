package systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.handlers

import systems.zlink.framework.channels.ZLinkRouteRequestContext
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkSuspendingRouteRequestHandler
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomRes

@ZLinkHandlerGroup("play-route")
class AllocateBingoRoomHandler(
    private val rooms: BingoRoomDirectory,
) : ZLinkSuspendingRouteRequestHandler<AllocateBingoRoomReq, AllocateBingoRoomRes> {
    override suspend fun handle(
        request: AllocateBingoRoomReq,
        context: ZLinkRouteRequestContext,
    ) = run {
        val reservation = rooms.allocate(
            request.actorId,
            request.mode,
            request.preferredOwnerNodeRid,
        )
        AllocateBingoRoomRes(reservation.roomId, reservation.ownerPlayNodeRid)
    }
}
