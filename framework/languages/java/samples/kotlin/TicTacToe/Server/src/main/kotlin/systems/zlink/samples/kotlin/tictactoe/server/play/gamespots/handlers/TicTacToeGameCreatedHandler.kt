package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers

import systems.zlink.contracts.messaging.Message
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGame

class TicTacToeGameCreatedHandler {
    fun handle(
        game: TicTacToeGame,
        createParts: List<Message>,
    ) {
        game.markCreated(createParts)
    }
}
