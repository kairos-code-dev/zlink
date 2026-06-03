package systems.zlink.samples.kotlin.bingo.server.play.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.shared.contracts.EnsurePlayerActorReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.EnsurePlayerActorRes

class EnsurePlayerActorHandler : ZLinkRequestHandler<EnsurePlayerActorReq, EnsurePlayerActorRes> {
    override fun handleAsync(
        request: EnsurePlayerActorReq,
        context: ZLinkRequestContext,
    ): CompletionStage<EnsurePlayerActorRes> =
        CompletableFuture.completedFuture(
            EnsurePlayerActorRes(request.actorId, SampleNames.PlayerActorType),
        )
}
