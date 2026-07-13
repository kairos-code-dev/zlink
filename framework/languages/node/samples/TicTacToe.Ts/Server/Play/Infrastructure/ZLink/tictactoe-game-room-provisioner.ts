import { Inject } from '@nestjs/common';
import { ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { TicTacToeGameSpot } from './Spots/TicTacToeGameSpot/tictactoe-game-spot';
import {
  RedisRoomRouteStore,
  TICTACTOE_SAMPLE_CONFIG
} from '../../../Configuration/redis-room-route-store';
import { SampleNames } from '../../../Configuration/sample-settings';
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
    const route = {
      roomId,
      routeChannelId: SampleNames.playSpotNode,
      ownerSpotNodeRid: this.config.playSpotNodeRid,
      spotRid: roomId,
      spotKind: 'User' as const
    };
    await this.routes.save(route);
    const stored = await this.routes.load(roomId);
    if (
      stored.routeChannelId !== route.routeChannelId ||
      stored.ownerSpotNodeRid !== route.ownerSpotNodeRid ||
      stored.spotRid !== route.spotRid
    ) {
      throw new Error(`Redis room route verification failed. roomId=${roomId}`);
    }
    console.log(
      `room-route=verified roomId=${roomId} routeChannelId=${stored.routeChannelId} ` +
      `ownerNodeRid=${stored.ownerSpotNodeRid} spotRid=${stored.spotRid} spotKind=${stored.spotKind}`
    );
  }
}

export { ZLinkTicTacToeGameRoomProvisioner };
