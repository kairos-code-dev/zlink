package systems.zlink.samples.kotlin.tictactoe.server.play.entryspot.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.handlers.ZLinkSpotPostActorJoined
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult
import systems.zlink.samples.kotlin.tictactoe.server.play.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.entryspot.PlayEntrySpot

class PlayEntrySpotActorJoinedHandler {
    @ZLinkSpotPostActorJoined
    suspend fun actorJoined(
        entrySpot: PlayEntrySpot,
        actor: PlayActor,
        info: ZLinkSpotActorChangeResult,
        cancellationToken: CancellationToken,
    ) {
    }
}
