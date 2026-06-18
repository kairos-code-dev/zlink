package systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.spots.handlers

import systems.zlink.framework.kotlin.ZLinkSuspendingSpotTimerHandler
import systems.zlink.framework.spots.ZLinkTimerTick
import systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.spots.TicTacToeGame

class TicTacToeGameTimerHandler() : ZLinkSuspendingSpotTimerHandler<TicTacToeGame> {
    override suspend fun handle(
        spot: TicTacToeGame,
        tick: ZLinkTimerTick,
    ) = run {
        spot.tick()
    }
}
