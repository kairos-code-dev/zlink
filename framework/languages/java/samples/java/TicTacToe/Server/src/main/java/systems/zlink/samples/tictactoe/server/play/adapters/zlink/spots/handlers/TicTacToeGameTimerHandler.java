package systems.zlink.samples.tictactoe.server.play.adapters.zlink.spots.handlers;

import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.spots.ZLinkTimerTick;
import systems.zlink.samples.tictactoe.server.play.adapters.zlink.spots.TicTacToeGame;

public final class TicTacToeGameTimerHandler implements ZLinkSpotTimerHandler<TicTacToeGame> {
    @Override
    public void handle(
        TicTacToeGame spot,
        ZLinkTimerTick tick) {
        spot.tick();
    }
}
