package systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.ZLinkAwait.await as awaitStage
import systems.zlink.framework.kotlin.await
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.handlers.PlayerWinMilestoneEventHandler
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.ObserveMilestoneRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlayerInfo
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlayerWinMilestoneEvent
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.WinMilestoneNotify

class PlayEntrySpot(
    private val context: ZLinkEntrySpotContext,
    private val settings: SampleSettings,
) : ZLinkEntrySpot<PlayActor> {
    private val milestoneObservers = mutableListOf<PlayActor>()

    override fun context(): ZLinkEntrySpotContext = context

    override fun configure() {
        context.handlers().addSubscribe(
            SampleNames.PlayerMilestoneTopic,
            PlayerWinMilestoneEventHandler::class.java,
        )
    }

    override fun onCreateActor(
        actor: PlayActor,
        createRequest: ZLinkMessage,
        cancellationToken: CancellationToken,
    ) {
        if (createRequest.isEmpty) {
            return
        }
        actor.applyPlayer(createRequest.decode(PlayerInfo::class.java))
    }

    override fun onActorJoin(
        actor: PlayActor,
        request: ZLinkMessage,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse =
        ZLinkSpotActorJoinResponse.accept()

    override fun onJoinedActor(
        actor: PlayActor,
        cancellationToken: CancellationToken,
    ) {
        if (actor.destroyAfterEntrySpotJoin) {
            awaitStage(context.destroyActor(actor))
        }
    }

    override fun onDisconnectActor(
        actor: PlayActor,
        cancellationToken: CancellationToken,
    ) {
        actor.markDisconnected()
        milestoneObservers.removeIf { it.actorId == actor.actorId }
    }

    fun observeMilestone(actor: PlayActor): ObserveMilestoneRes {
        rememberObserver(actor)
        return ObserveMilestoneRes(true)
    }

    fun notifyMilestone(event: PlayerWinMilestoneEvent) {
        val payload = WinMilestoneNotify(
            roomId = event.roomId,
            actorId = event.actorId,
            displayName = event.displayName,
            wins = event.wins,
            receivingSpotNodeRid = settings.playSpotNodeRid,
        )
        milestoneObservers.toList().forEach { observer ->
            observer.context().boundSession()
                .send(payload)
                .await()
        }
    }

    private fun rememberObserver(actor: PlayActor) {
        milestoneObservers.removeIf { it.actorId == actor.actorId }
        milestoneObservers += actor
    }
}
