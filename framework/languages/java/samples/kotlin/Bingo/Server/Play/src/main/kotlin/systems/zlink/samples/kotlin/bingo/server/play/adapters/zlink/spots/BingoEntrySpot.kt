package systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots

import java.util.concurrent.CompletionStage
import java.util.concurrent.CompletableFuture
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.handlers.MatchBingoActorHandler

class BingoEntrySpot(
    private val context: ZLinkEntrySpotContext,
) : ZLinkEntrySpot {
    override fun context(): ZLinkEntrySpotContext = context

    override fun configure() {
        context.handlers().addHandler(MatchBingoActorHandler::class.java)
    }

    override fun onPostActorJoinedAsync(
        actor: ZLinkActor,
        cancellationToken: CancellationToken,
    ): CompletionStage<Void> {
        return CompletableFuture.completedFuture(null)
    }

    override fun onActorLeftAsync(
        actor: ZLinkActor,
        cancellationToken: CancellationToken,
    ): CompletionStage<Void> {
        return CompletableFuture.completedFuture(null)
    }
}
