package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.handlers

import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.TicTacToeMatchRoom

class TicTacToeGameSpotActorLeftHandler {
    fun handle(room: TicTacToeMatchRoom, actorId: String): TicTacToeMatchRoom =
        if (room.lastActorId == actorId) room.copy(lastActorId = "") else room
}
