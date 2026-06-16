import type {
  ZLinkSpotTimerHandler,
  ZLinkTimerTick
} from '@zlink-systems/framework';
import type { TicTacToeGameSpot } from '../tictactoe-game-spot';

class TicTacToeGameTimerHandler implements ZLinkSpotTimerHandler<TicTacToeGameSpot> {
  async handle(spot: TicTacToeGameSpot, tick: ZLinkTimerTick): Promise<void> {
    void tick;
    await spot.tick();
  }
}

export { TicTacToeGameTimerHandler };
