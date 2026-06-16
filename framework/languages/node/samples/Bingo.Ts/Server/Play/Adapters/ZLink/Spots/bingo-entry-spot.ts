import { Inject } from '@nestjs/common';
import { matchBingoRes } from '../../../../../Shared/Contracts/messages';
import { BingoRoomAllocator } from '../../../Application/RoomAllocation/bingo-room-allocator';
import type {
  ZLinkEntrySpot,
  ZLinkEntrySpotContext
} from '@zlink-systems/framework';
import type { BingoRoomAllocator as BingoRoomAllocatorType } from '../../../Application/RoomAllocation/bingo-room-allocator';
import type {
  MatchBingoReq,
  MatchBingoRes
} from '../../../../../Shared/Contracts/messages';
import type { PlayerActor as PlayerActorType } from '../Actors/player-actor';

class BingoEntrySpot implements ZLinkEntrySpot<PlayerActorType> {
  readonly context!: ZLinkEntrySpotContext;

  constructor(private readonly roomDirectory: BingoRoomAllocatorType) {}

  async matchActor(actor: PlayerActorType, request: MatchBingoReq): Promise<MatchBingoRes> {
    const roomId = await this.roomDirectory.allocate(request.mode);
    const joined = await actor.context.joinSpot(roomId).submit<void>();
    if (joined.resultCode !== 0) {
      throw new Error(`Room ${roomId} rejected actor '${actor.actorId}'.`);
    }
    const state = await this.roomDirectory.executeInRoom(roomId, (room) => room.snapshot());
    return matchBingoRes(roomId, state);
  }

  async onJoinActor(actor: PlayerActorType): Promise<void> {
    if (!actor.destroyAfterEntrySpotJoin) {
      return;
    }
    await this.context.destroyActor(actor);
  }

  async onCreateActor(actor: PlayerActorType): Promise<void> {
    void actor;
  }

  async onLeaveActor(actor: PlayerActorType): Promise<void> {
    void actor;
  }

  async onDisconnectActor(actor: PlayerActorType): Promise<void> {
    actor.markDisconnected();
  }
}

Inject(BingoRoomAllocator)(BingoEntrySpot, undefined, 0);

export { BingoEntrySpot };
