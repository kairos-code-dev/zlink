package systems.zlink.samples.kotlin.tictactoe.server.play.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGameDirectory

class CreateGameHandler {
    fun createAsync(gameName: String): CompletionStage<String> =
        CompletableFuture.completedFuture(TicTacToeGameDirectory.create(gameName).gameId)
}
