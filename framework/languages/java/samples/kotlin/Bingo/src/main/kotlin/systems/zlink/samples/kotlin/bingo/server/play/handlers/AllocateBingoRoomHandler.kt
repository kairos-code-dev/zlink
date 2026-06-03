package systems.zlink.samples.kotlin.bingo.server.play.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AllocateBingoRoomRes

class AllocateBingoRoomHandler : ZLinkRequestHandler<AllocateBingoRoomReq, AllocateBingoRoomRes> {
    override fun handleAsync(
        request: AllocateBingoRoomReq,
        context: ZLinkRequestContext,
    ): CompletionStage<AllocateBingoRoomRes> =
        CompletableFuture.completedFuture(AllocateBingoRoomRes("room-1"))
}
