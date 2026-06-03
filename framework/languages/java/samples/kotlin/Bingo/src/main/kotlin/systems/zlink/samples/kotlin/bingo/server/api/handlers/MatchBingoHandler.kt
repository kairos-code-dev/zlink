package systems.zlink.samples.kotlin.bingo.server.api.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiRes

@ZLinkHandlerGroup("api")
class MatchBingoHandler {
    @ZLinkRequest(packetName = "MatchBingo")
    fun handleAsync(request: MatchBingoApiReq): CompletionStage<MatchBingoApiRes> =
        CompletableFuture.completedFuture(MatchBingoApiRes("room-1"))
}
