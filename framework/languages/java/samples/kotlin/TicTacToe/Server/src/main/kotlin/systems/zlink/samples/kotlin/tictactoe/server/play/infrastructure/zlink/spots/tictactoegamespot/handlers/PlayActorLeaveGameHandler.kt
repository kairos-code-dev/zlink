package systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.tictactoegamespot.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkSpotActorSend
import systems.zlink.framework.spots.ZLinkSpotActorSendContext
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.tictactoegamespot.TicTacToeGame
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.LeaveGameMsg

@ZLinkHandlerGroup(SampleNames.PlayActor)
class PlayActorLeaveGameHandler {
    @ZLinkSpotActorSend
    fun leaveGame(
        spot: TicTacToeGame,
        actor: PlayActor,
        context: ZLinkSpotActorSendContext,
        request: LeaveGameMsg,
        cancellationToken: CancellationToken,
    ) {
        spot.leaveGame(actor, request.roomId)
    }
}
