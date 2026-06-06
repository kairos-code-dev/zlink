package systems.zlink.samples.kotlin.tictactoe.server.play.entryspot.handlers

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkSpotActorRequest
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.play.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.entryspot.PlayEntrySpot
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
        cancellationToken: CancellationToken,
    ): JoinGameRes {
        val result = actor.context()
            .joinSpot(RoutingId.fromHex(request.gameId), TicTacToeGameJoinReq(request.gameId, actor.actorId()))
            .submitAsync(TicTacToeGameJoinRes::class.java)
            .await()
        actor.joinGame(request.gameId)
        return JoinGameRes(result.reply().state)
    }
}
