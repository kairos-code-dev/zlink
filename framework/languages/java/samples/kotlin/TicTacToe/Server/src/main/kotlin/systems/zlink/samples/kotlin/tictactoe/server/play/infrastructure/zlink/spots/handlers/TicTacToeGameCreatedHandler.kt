package systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.handlers

import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.TicTacToeGame

class TicTacToeGameCreatedHandler {
    fun handle(
        game: TicTacToeGame,
        request: ZLinkMessage,
    ) {
        game.markCreated(request)
    }
}
