package systems.zlink.samples.kotlin.tictactoe.server.play.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGameCatalog

class CreateGameHandler {
    fun createAsync(gameName: String): CompletionStage<String> =
        CompletableFuture.completedFuture(TicTacToeGameCatalog.create(gameName).gameId)
}
