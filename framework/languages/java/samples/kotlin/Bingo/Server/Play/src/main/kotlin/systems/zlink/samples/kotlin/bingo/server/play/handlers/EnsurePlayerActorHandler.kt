package systems.zlink.samples.kotlin.bingo.server.play.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.shared.contracts.EnsurePlayerActorReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.EnsurePlayerActorRes

@ZLinkHandlerGroup("play")
class EnsurePlayerActorHandler {
    @ZLinkRequest(packetName = "EnsurePlayerActor")
    fun handleAsync(request: EnsurePlayerActorReq): CompletionStage<EnsurePlayerActorRes> =
        CompletableFuture.completedFuture(
            EnsurePlayerActorRes(request.actorId, SampleNames.PlayerActorType),
        )
}
