package systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.entryspot.handlers

import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.kotlin.awaitJoin
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkSpotActorRequest
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.entryspot.PlayEntrySpot
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameRes
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.TicTacToeGameJoinReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.TicTacToeGameJoinRes

@ZLinkHandlerGroup(SampleNames.PlayActor)
class PlayActorJoinGameHandler {
    @ZLinkSpotActorRequest
    suspend fun joinGame(
        entrySpot: PlayEntrySpot,
        actor: PlayActor,
        context: ZLinkSpotActorRequestContext,
        request: JoinGameReq,
    ): JoinGameRes {
        val result = actor.context()
            .joinSpot(RoutingId.from(request.roomId), TicTacToeGameJoinReq(request.roomId, actor.requirePlayer()))
            .awaitJoin(TicTacToeGameJoinRes::class.java)
        actor.joinGame(request.roomId)
        return JoinGameRes(result.reply().state)
    }
}
