package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers

import systems.zlink.contracts.messaging.Message
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGame

class TicTacToeGameCreatedHandler {
    fun handle(
        game: TicTacToeGame,
        request: Message,
    ) {
        game.markCreated(request)
    }
}
