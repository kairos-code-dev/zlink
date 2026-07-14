import { Inject } from '@nestjs/common';
import { ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { TicTacToeGameSpot } from './Spots/TicTacToeGameSpot/tictactoe-game-spot';
import type { ZLinkSpotManager } from '@zlink-systems/framework';
import type { TicTacToeGameRoomProvisioner } from '../../Application/GameCreation/tictactoe-game-creator';

class ZLinkTicTacToeGameRoomProvisioner implements TicTacToeGameRoomProvisioner {
  constructor(@Inject(ZLINK_SPOT_MANAGER) private readonly spotManager: ZLinkSpotManager) {}

  async provision(roomId: string): Promise<void> {
    await this.spotManager.getOrCreate(TicTacToeGameSpot, roomId);
  }
}

export { ZLinkTicTacToeGameRoomProvisioner };
