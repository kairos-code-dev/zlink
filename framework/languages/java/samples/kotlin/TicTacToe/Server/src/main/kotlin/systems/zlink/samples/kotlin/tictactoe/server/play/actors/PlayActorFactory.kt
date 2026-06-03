package systems.zlink.samples.kotlin.tictactoe.server.play.actors

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.actors.ZLinkActorFactory

class PlayActorFactory : ZLinkActorFactory {
    override fun createAsync(
        actorId: String,
        context: ZLinkActorContext,
    ): CompletionStage<ZLinkActor> =
        CompletableFuture.completedFuture(PlayActor(actorId, context))
}
