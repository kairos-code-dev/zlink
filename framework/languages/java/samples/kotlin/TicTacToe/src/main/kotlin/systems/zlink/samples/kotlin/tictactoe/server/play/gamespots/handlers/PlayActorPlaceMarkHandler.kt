package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers

import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGameDirectory
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkRes

class PlayActorPlaceMarkHandler {
    fun placeMark(gameId: String, actorId: String, cell: Int): PlaceMarkRes =
        TicTacToeGameDirectory.get(gameId).placeMark(actorId, cell)
}
