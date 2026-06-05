package systems.zlink.samples.kotlin.tictactoe.server.play.entryspot.handlers

import java.util.concurrent.CompletionStage
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
    fun joinGame(
        entrySpot: PlayEntrySpot,
        actor: PlayActor,
        context: ZLinkSpotActorRequestContext,
        request: JoinGameReq,
        cancellationToken: CancellationToken,
    ): CompletionStage<JoinGameRes> =
        actor.context()
            .joinSpot(RoutingId.fromHex(request.gameId), TicTacToeGameJoinReq(request.gameId, actor.actorId()))
            .submitAsync(TicTacToeGameJoinRes::class.java)
            .thenApply { result ->
                actor.joinGame(request.gameId)
                JoinGameRes(result.reply().state)
            }
}
