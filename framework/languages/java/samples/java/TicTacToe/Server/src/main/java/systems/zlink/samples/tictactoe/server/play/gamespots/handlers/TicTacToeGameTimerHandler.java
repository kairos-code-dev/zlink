package systems.zlink.samples.tictactoe.server.play.gamespots.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.spots.ZLinkTimerTick;
import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGame;

public final class TicTacToeGameTimerHandler implements ZLinkSpotTimerHandler<TicTacToeGame> {
    @Override
    public CompletionStage<Void> handleAsync(
        TicTacToeGame spot,
        ZLinkTimerTick tick) {
        return spot.tickAsync();
    }
}
