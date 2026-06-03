package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers

import java.util.concurrent.CompletionStage
import systems.zlink.framework.spots.ZLinkSpotTimerHandler
import systems.zlink.framework.spots.ZLinkTimerTick
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGame

class TicTacToeGameTimerHandler : ZLinkSpotTimerHandler<TicTacToeGame> {
    override fun handleAsync(
        spot: TicTacToeGame,
        tick: ZLinkTimerTick,
    ): CompletionStage<Void> = spot.tickAsync()
}
