package systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.spots

import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.await
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.spots.handlers.PlayerWinMilestoneEventHandler
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.ObserveMilestoneRes
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
        cancellationToken: CancellationToken,
    ) = Unit

    override fun onActorJoin(
        actor: PlayActor,
        request: Message,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse =
        ZLinkSpotActorJoinResponse.accept(Message.from(ByteArray(0)))

    override fun onJoinedActor(
        actor: PlayActor,
        cancellationToken: CancellationToken,
    ) {
        if (actor.destroyAfterEntrySpotJoin) {
            context.destroyActor(actor).toCompletableFuture().join()
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
        println("actor: ObserveMilestoneReq completed. actor=${actor.actorId}")
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
