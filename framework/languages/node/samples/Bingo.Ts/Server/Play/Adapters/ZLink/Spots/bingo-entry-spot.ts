import { Inject } from '@nestjs/common';
import { Message } from '@zlink-systems/zlink';
import { bingoRoomJoinReq, matchBingoRes } from '../../../../../Shared/Contracts/messages';
import { BingoRoomAllocator } from '../../../Application/RoomAllocation/bingo-room-allocator';
import type {
  ZLinkEntrySpot,
  ZLinkEntrySpotContext
} from '@zlink-systems/framework';
import type { BingoRoomAllocator as BingoRoomAllocatorType } from '../../../Application/RoomAllocation/bingo-room-allocator';
import type {
  MatchBingoReq,
  MatchBingoRes,
  RoomJoinError,
  StateEnvelope
} from '../../../../../Shared/Contracts/messages';
import type { PlayerActor as PlayerActorType } from '../Actors/player-actor';

type BingoJoinReply = Partial<StateEnvelope & RoomJoinError>;

class BingoEntrySpot implements ZLinkEntrySpot<PlayerActorType> {
  readonly context!: ZLinkEntrySpotContext;

  constructor(private readonly roomDirectory: BingoRoomAllocatorType) {}

  async matchActor(actor: PlayerActorType, request: MatchBingoReq): Promise<MatchBingoRes> {
    const roomId = await this.roomDirectory.allocate(request.mode);
    const joinRequest = Message.from(bingoRoomJoinReq(roomId, actor.actorId, actor.displayName));
    try {
      const joined = await actor.context.joinSpot(roomId, joinRequest).submit<BingoJoinReply>();
      const reply: BingoJoinReply = joined.reply ?? {};
      if (joined.resultCode !== 0) {
        throw new Error(reply.error ?? `Room ${roomId} rejected actor '${actor.actorId}'.`);
      }
      return matchBingoRes(roomId, reply.state);
    } finally {
      joinRequest.close();
    }
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
