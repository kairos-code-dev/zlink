import { Inject } from '@nestjs/common';
import { randomUUID } from 'node:crypto';
import { createGameRes } from '../../../../Shared/Contracts/messages';
import { SampleDefaults } from '../../../Configuration/sample-settings';
import { TICTACTOE_SAMPLE_CONFIG } from '../../../Configuration/sample-config';
import type {
  CreateGameRes
} from '../../../../Shared/Contracts/messages';
import type { TicTacToeSampleConfig } from '../../../Configuration/sample-config';

const TICTACTOE_GAME_ROOM_PROVISIONER = Symbol('TICTACTOE_GAME_ROOM_PROVISIONER');

interface TicTacToeGameRoomProvisioner {
  provision(roomId: string): Promise<void>;
}

class TicTacToeGameCreator {
  constructor(
    @Inject(TICTACTOE_GAME_ROOM_PROVISIONER) private readonly rooms: TicTacToeGameRoomProvisioner,
    @Inject(TICTACTOE_SAMPLE_CONFIG) private readonly config: TicTacToeSampleConfig,
  ) {}

  async create(gameName: string): Promise<CreateGameRes> {
    const roomId = `room-${randomUUID().replaceAll('-', '')}`;
    await this.rooms.provision(roomId);
    const playNodes = this.config.playEndpoints.map((endpoint, index) => ({
      streamEndpoint: endpoint,
      spotNodeRid: `play-node-${index + 1}`
    }));
    const ownerPlayEndpoint = this.config.playStreamEndpoint;
    console.log(
      `game-created roomId=${roomId} ownerPlayEndpoint=${ownerPlayEndpoint} nodeRid=${this.config.playSpotNodeRid}`
    );
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

export { TICTACTOE_GAME_ROOM_PROVISIONER, TicTacToeGameCreator };
export type { TicTacToeGameRoomProvisioner };
