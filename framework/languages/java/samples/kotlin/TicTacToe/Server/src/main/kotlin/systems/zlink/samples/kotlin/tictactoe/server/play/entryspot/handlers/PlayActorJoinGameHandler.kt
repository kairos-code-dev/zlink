package systems.zlink.samples.kotlin.tictactoe.server.play.entryspot.handlers

import java.util.concurrent.CompletionStage
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkSpotActorRequest
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.play.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameRes

@ZLinkHandlerGroup(SampleNames.PlayActor)
class PlayActorJoinGameHandler {
    @ZLinkSpotActorRequest
    fun joinGame(
        actor: PlayActor,
        request: JoinGameReq,
    ): CompletionStage<JoinGameRes> =
        actor.context()
            .joinSpot(RoutingId.fromHex(request.gameId), request)
            .submitAsync(JoinGameRes::class.java)
            .thenApply { result -> result.reply() }
}
