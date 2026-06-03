package systems.zlink.samples.kotlin.bingo.server.play.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomRes

@ZLinkHandlerGroup("play")
class AllocateBingoRoomHandler {
    @ZLinkRequest(packetName = "AllocateBingoRoom")
    fun handleAsync(request: AllocateBingoRoomReq): CompletionStage<AllocateBingoRoomRes> =
        CompletableFuture.completedFuture(AllocateBingoRoomRes("room-1"))
}
