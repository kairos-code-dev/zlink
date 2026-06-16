import { Inject } from '@nestjs/common';
import { randomUUID } from 'node:crypto';
import { ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { createGameRes } from '../../../../Shared/Contracts/messages';
import { TicTacToeGameSpot } from '../../Adapters/ZLink/Spots/tictactoe-game-spot';
import { PLAY_STREAM_ENDPOINT } from '../../play-tokens';
import type {
  ZLinkSpotManager
} from '@zlink-systems/framework';
import { ZLinkSpotCreateState } from '@zlink-systems/framework';
import type {
  CreateGameRes
} from '../../../../Shared/Contracts/messages';

class TicTacToeGameCreator {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spotManager: ZLinkSpotManager,
    @Inject(PLAY_STREAM_ENDPOINT) private readonly playEndpoint: string
  ) {}

  async create(gameName: string): Promise<CreateGameRes> {
    const roomId = `room-${randomUUID().replaceAll('-', '')}`;
    const created = await this.spotManager.getOrCreate(TicTacToeGameSpot, roomId);
    if (created.state !== ZLinkSpotCreateState.Created) {
      throw new Error('TicTacToe game spot creation was rejected.');
    }
    return createGameRes(roomId, gameName, this.playEndpoint);
  }
}

export { TicTacToeGameCreator };
