import { Inject } from '@nestjs/common';
import { ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { TicTacToeGameSpot } from './Spots/tictactoe-game-spot';
import {
  RedisRoomRouteStore,
  TICTACTOE_SAMPLE_CONFIG
} from '../../../Configuration/redis-room-route-store';
import type { ZLinkSpotManager } from '@zlink-systems/framework';
import type { TicTacToeSampleConfig } from '../../../Configuration/sample-config';
import type { TicTacToeGameRoomProvisioner } from '../../Application/GameCreation/tictactoe-game-creator';

class ZLinkTicTacToeGameRoomProvisioner implements TicTacToeGameRoomProvisioner {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spotManager: ZLinkSpotManager,
    @Inject(TICTACTOE_SAMPLE_CONFIG) private readonly config: TicTacToeSampleConfig,
    private readonly routes: RedisRoomRouteStore
  ) {}

  async provision(roomId: string): Promise<void> {
    await this.spotManager.getOrCreate(TicTacToeGameSpot, roomId);
    await this.routes.save({
      roomId,
      ownerPlayEndpoint: this.config.playStreamEndpoint,
      ownerSpotEndpoint: this.config.playSpotEndpoint,
      ownerSpotNodeRid: this.config.playSpotNodeRid
    });
  }
}

export { ZLinkTicTacToeGameRoomProvisioner };
