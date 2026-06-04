package systems.zlink.samples.kotlin.bingo.server.play.handlers

import java.util.concurrent.CompletionStage
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.samples.kotlin.bingo.server.play.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.shared.configuration.SampleTopology
import systems.zlink.samples.kotlin.bingo.shared.contracts.ActorRefSnapshot
import systems.zlink.samples.kotlin.bingo.shared.contracts.EnsurePlayerActorReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.EnsurePlayerActorRes

@ZLinkHandlerGroup("play")
class EnsurePlayerActorHandler(
    private val actors: ZLinkActorManager,
) {
    @ZLinkRequest(packetName = "EnsurePlayerActor")
    fun handleAsync(request: EnsurePlayerActorReq): CompletionStage<EnsurePlayerActorRes> =
        actors.getOrCreateAsync(request.actorId, SampleNames.PlayerActorType)
            .thenCompose { actor ->
                if (actor is PlayerActor) {
                    actor.setDisplayName(request.displayName)
                }
                actor.context()
                    .joinEntrySpot(RoutingId.from(SampleTopology.PlayRid))
                    .timeout(SampleTimings.RequestTimeout)
                    .submitAsync()
            }
            .thenApply { joined ->
                EnsurePlayerActorRes(
                    request.actorId,
                    SampleNames.PlayerActorType,
                    toSnapshot(joined),
                )
            }

    private fun toSnapshot(actor: ZLinkActorRef): ActorRefSnapshot =
        ActorRefSnapshot(
            actor.nodeRid().toBytes(),
            actor.actorId(),
            actor.epoch(),
        )
}
