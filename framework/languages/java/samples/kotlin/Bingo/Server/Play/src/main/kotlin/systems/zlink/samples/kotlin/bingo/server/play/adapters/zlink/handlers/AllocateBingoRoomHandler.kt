package systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomRes

@ZLinkHandlerGroup("play")
class AllocateBingoRoomHandler(
    private val rooms: BingoRoomDirectory,
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkRequestHandler<AllocateBingoRoomReq, AllocateBingoRoomRes> {
    override fun handle(
        request: AllocateBingoRoomReq,
        context: ZLinkRequestContext,
    ) = coroutines.blocking {
        AllocateBingoRoomRes(rooms.allocate(request.actorId, request.mode))
    }
}
