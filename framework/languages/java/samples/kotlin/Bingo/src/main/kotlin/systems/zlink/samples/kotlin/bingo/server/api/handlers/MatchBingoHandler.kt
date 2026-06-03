package systems.zlink.samples.kotlin.bingo.server.api.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.MatchBingoApiRes

class MatchBingoHandler : ZLinkRequestHandler<MatchBingoApiReq, MatchBingoApiRes> {
    override fun handleAsync(
        request: MatchBingoApiReq,
        context: ZLinkRequestContext,
    ): CompletionStage<MatchBingoApiRes> =
        CompletableFuture.completedFuture(MatchBingoApiRes("room-1"))
}
