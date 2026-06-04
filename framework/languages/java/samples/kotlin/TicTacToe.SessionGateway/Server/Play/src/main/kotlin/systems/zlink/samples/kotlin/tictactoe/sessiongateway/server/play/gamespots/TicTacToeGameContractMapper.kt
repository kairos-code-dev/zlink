package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots

import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.contracts.GameStateChanged

class TicTacToeGameContractMapper {
    fun toGameStateChanged(state: TicTacToeGameModels.State): GameStateChanged =
        GameStateChanged(state.value)
}
