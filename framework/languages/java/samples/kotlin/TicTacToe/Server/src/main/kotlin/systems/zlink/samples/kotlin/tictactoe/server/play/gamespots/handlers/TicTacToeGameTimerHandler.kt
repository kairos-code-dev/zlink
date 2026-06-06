package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers

import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.kotlin.ZLinkCoroutineSpotTimerHandler
import systems.zlink.framework.spots.ZLinkTimerTick
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGame

class TicTacToeGameTimerHandler(
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSpotTimerHandler<TicTacToeGame>(coroutines) {
    override suspend fun handle(
        spot: TicTacToeGame,
        tick: ZLinkTimerTick,
    ) {
        spot.tick()
    }
}
