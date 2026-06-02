package systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots;

import systems.zlink.samples.tictactoe.sessiongateway.shared.contracts.Messages;

public final class TicTacToeGameContractMapper {
    public Messages.GameStateChanged toGameStateChanged(TicTacToeGameModels.State state) {
        return new Messages.GameStateChanged(state.value());
    }
}
