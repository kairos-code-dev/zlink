import { randomUUID } from 'node:crypto';
import { Inject } from '@nestjs/common';
import { createRoomSettings } from '../../Domain/Bingo/bingo-room-models';
import { BINGO_MATCH_QUEUE } from './bingo-match-queue';
import type { BingoMode } from '../../Domain/Bingo/bingo-room-models';
import type { BingoRoomSettings } from '../../Domain/Bingo/bingo-room-models';
import type { BingoMatchQueue } from './bingo-match-queue';

type PlayerIdentity = {
  actorId: string;
};

type BingoRoomAllocation = {
  roomId: string;
  created: boolean;
  settings: BingoRoomSettings;
};

class BingoRoomAllocator {
  constructor(
    @Inject(BINGO_MATCH_QUEUE) private readonly matchQueue: BingoMatchQueue
  ) {}

  async allocate(
    actor: PlayerIdentity,
    mode: BingoMode = 'two-player'
  ): Promise<BingoRoomAllocation> {
    const settings = createRoomSettings(mode);
    const reserved = await this.matchQueue.reserve({
      mode,
      actorId: actor.actorId,
      requiredPlayers: settings.requiredPlayers,
      createRoomId: () => `bingo-room-${randomUUID().replaceAll('-', '')}`
    });
    return {
      roomId: reserved.record.roomId,
      created: reserved.created,
      settings
    };
  }
}

export { BingoRoomAllocator };
export type { BingoRoomAllocation };
