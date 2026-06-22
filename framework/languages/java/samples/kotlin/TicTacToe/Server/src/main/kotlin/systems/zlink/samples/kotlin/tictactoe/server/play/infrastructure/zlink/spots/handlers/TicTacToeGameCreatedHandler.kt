package systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.handlers

import systems.zlink.contracts.messaging.Message
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.TicTacToeGame

class TicTacToeGameCreatedHandler {
    fun handle(
        game: TicTacToeGame,
        request: Message,
    ) {
        game.markCreated(request)
    }
}
