package systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkSpotActorRequest
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.PlayEntrySpot
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.ObserveMilestoneReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.ObserveMilestoneRes

@ZLinkHandlerGroup(SampleNames.PlayActor)
class PlayActorObserveMilestoneHandler {
    @ZLinkSpotActorRequest
    fun observe(
        entrySpot: PlayEntrySpot,
        actor: PlayActor,
        context: ZLinkSpotActorRequestContext,
        request: ObserveMilestoneReq,
        cancellationToken: CancellationToken,
    ): ObserveMilestoneRes = entrySpot.observeMilestone(actor)
}
