package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.handlers

import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.TicTacToeMatchRoom

class TicTacToeGameSpotActorJoinedHandler {
    fun handle(room: TicTacToeMatchRoom, actorId: String): TicTacToeMatchRoom =
        room.copy(lastActorId = actorId)
}
