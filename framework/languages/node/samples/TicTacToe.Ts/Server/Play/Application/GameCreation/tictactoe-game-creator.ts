import { Inject } from '@nestjs/common';
import { randomUUID } from 'node:crypto';
import { ZLINK_FRAMEWORK_RUNTIME, ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { createGameRes } from '../../../../Shared/Contracts/messages';
import { SampleDefaults } from '../../../Configuration/sample-settings';
import {
  RedisRoomRouteStore,
  TICTACTOE_SAMPLE_CONFIG
} from '../../../Configuration/redis-room-route-store';
import { TicTacToeGameSpot } from '../../Adapters/ZLink/Spots/tictactoe-game-spot';
import type {
  ZLinkSpotManager
} from '@zlink-systems/framework';
import { ZLinkSpotCreateState } from '@zlink-systems/framework';
import type {
  CreateGameRes
} from '../../../../Shared/Contracts/messages';
import type { TicTacToeSampleConfig } from '../../../Configuration/sample-config';

class TicTacToeGameCreator {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spotManager: ZLinkSpotManager,
    @Inject(ZLINK_FRAMEWORK_RUNTIME) private readonly runtime: {
      spotNodeRuntime?: {
        primaryNode?: {
          subjects(): unknown[];
          status(): unknown;
        };
      };
    },
    @Inject(TICTACTOE_SAMPLE_CONFIG) private readonly config: TicTacToeSampleConfig,
    private readonly routes: RedisRoomRouteStore
  ) {}

  async create(gameName: string): Promise<CreateGameRes> {
    const roomId = `room-${randomUUID().replaceAll('-', '')}`;
    const created = await this.spotManager.getOrCreate(TicTacToeGameSpot, roomId);
    if (created.state !== ZLinkSpotCreateState.Created) {
      throw new Error('TicTacToe game spot creation was rejected.');
    }
    const playNodes = this.config.playEndpoints.map((endpoint, index) => ({
      streamEndpoint: endpoint,
      spotNodeRid: `play-node-${index + 1}`
    }));
    const ownerPlayEndpoint = this.config.playStreamEndpoint;
    console.log(
      `game-created roomId=${roomId} ownerPlayEndpoint=${ownerPlayEndpoint} nodeRid=${this.config.playSpotNodeRid}`
    );
    await this.routes.save({
      roomId,
      ownerPlayEndpoint,
      ownerSpotEndpoint: this.config.playSpotEndpoint,
      ownerSpotNodeRid: this.config.playSpotNodeRid
    });
    return createGameRes(
      roomId,
      gameName,
      ownerPlayEndpoint,
      this.config.playEndpoints,
      playNodes,
      SampleDefaults.requiredLevel
    );
  }
}

export { TicTacToeGameCreator };
