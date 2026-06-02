package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers

import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGameCatalog
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkRes

class PlayActorPlaceMarkHandler {
    fun placeMark(gameId: String, actorId: String, cell: Int): PlaceMarkRes =
        TicTacToeGameCatalog.get(gameId).placeMark(actorId, cell)
}
