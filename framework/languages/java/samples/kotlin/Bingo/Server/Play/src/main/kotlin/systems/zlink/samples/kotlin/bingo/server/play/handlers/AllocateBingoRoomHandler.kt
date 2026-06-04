package systems.zlink.samples.kotlin.bingo.server.play.handlers

import java.util.concurrent.CompletionStage
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomRes

@ZLinkHandlerGroup("play")
class AllocateBingoRoomHandler(
    private val rooms: BingoRoomDirectory,
) {
    @ZLinkRequest(packetName = "AllocateBingoRoomReq")
    fun handleAsync(request: AllocateBingoRoomReq): CompletionStage<AllocateBingoRoomRes> {
        return rooms.allocateAsync(request.actorId, request.mode)
            .thenApply { AllocateBingoRoomRes(it) }
    }
}
