import { Inject } from '@nestjs/common';
import { ModuleRef } from '@nestjs/core';
import { randomUUID } from 'node:crypto';
import { ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { BINGO_SAMPLE_CONFIG } from '../../../Configuration/sample-config';
import { RedisBingoMatchQueue } from './redis-bingo-match-queue';
import { createRoomSettings } from '../../Domain/Bingo/bingo-room-models';
import { BingoRoomSpot } from '../../Adapters/ZLink/Spots/bingo-room-spot';
import type {
  ZLinkSpotManager
} from '@zlink-systems/framework';
import { ZLinkSpotCreateState } from '@zlink-systems/framework';
import { BingoModes } from '../../../../Shared/Contracts/messages';
import type { BingoRoomSpot as BingoRoomSpotType } from '../../Adapters/ZLink/Spots/bingo-room-spot';
import type { BingoRoomSettings } from '../../Domain/Bingo/bingo-room-models';
import type {
  PlayerIdentity,
  BingoMode
} from '../../../../Shared/Contracts/messages';
import type { BingoSampleConfig } from '../../../Configuration/sample-config';

type BingoRoomAllocation = {
  roomId: string;
  ownerPlayNodeRid: string;
};

class BingoRoomAllocator {
  private readonly matchQueue: RedisBingoMatchQueue;

  constructor(
    private readonly moduleRef: ModuleRef,
    @Inject(BINGO_SAMPLE_CONFIG) private readonly config: BingoSampleConfig
  ) {
    this.matchQueue = new RedisBingoMatchQueue(config.redisEndpoint, config.redisKeyPrefix);
  }

  async allocate(actor: PlayerIdentity, mode: BingoMode = BingoModes.twoPlayer): Promise<BingoRoomAllocation> {
    const settings = createRoomSettings(mode);
    const reserved = await this.matchQueue.reserve({
      mode,
      actorId: actor.actorId,
      ownerPlayNodeRid: this.config.playSpotNodeRid,
      requiredPlayers: settings.requiredPlayers,
      createRoomId: () => `bingo-room-${randomUUID().replaceAll('-', '')}`
    });
    if (reserved.created) {
      const created = await this.spotManager().getOrCreate(BingoRoomSpot, reserved.record.roomId);
      if (created.state !== ZLinkSpotCreateState.Created) {
        throw new Error('Bingo room creation was rejected.');
      }
      await this.executeInRoom(created.spotRid, (room) => {
        room.initializeRoom(settings);
      });
    }
    return {
      roomId: reserved.record.roomId,
      ownerPlayNodeRid: reserved.record.ownerPlayNodeRid
    };
  }

  async executeInRoom<TResult>(
    roomId: string,
    operation: (room: BingoRoomSpotType) => TResult | Promise<TResult>
  ): Promise<TResult> {
    return await this.spotManager().executeOnSpot<BingoRoomSpotType, TResult>(BingoRoomSpot, roomId, operation);
  }

  private spotManager(): ZLinkSpotManager {
    return this.moduleRef.get(ZLINK_SPOT_MANAGER, { strict: false }) as ZLinkSpotManager;
  }
}

export { BingoRoomAllocator };
export type { BingoRoomAllocation };
