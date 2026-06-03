package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.handlers

import java.util.concurrent.CompletionStage
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.configuration.SampleNames

@ZLinkHandlerGroup("play")
class EnsurePlayerActorHandler(
    private val actors: ZLinkActorManager,
) {
    @ZLinkRequest(packetName = "EnsurePlayerActor")
    fun handleAsync(
        actorId: String,
    ): CompletionStage<String> =
        actors.getOrCreateAsync(actorId, SampleNames.PlayerActorType)
            .thenCompose { actor ->
                actor.context()
                    .joinEntrySpot(RoutingId.from(SampleNames.EntrySpotRoutingId))
                    .submitAsync()
                    .thenApply { joined -> snapshot(actor, joined) }
            }

    companion object {
        private fun snapshot(actor: ZLinkActor, ref: ZLinkActorRef): String =
            "${ref.nodeRid().toHex()}|${actor.actorId()}|${ref.epoch()}"
    }
}
