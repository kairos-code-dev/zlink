package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.contracts.messaging.Message
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGame

class TicTacToeGameCreatedHandler {
    fun handleAsync(
        game: TicTacToeGame,
        createParts: List<Message>,
    ): CompletionStage<Void> {
        game.markCreated(createParts)
        return CompletableFuture.completedFuture(null)
    }
}
