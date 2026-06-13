const { Inject } = require('@nestjs/common');
const {
  bingoRoomJoinReq,
  createProtobufMessage,
  matchBingoRes,
  readProtobufMessage
} = require('../../../../../Shared/Contracts/messages');
const { BingoRoomAllocator } = require('../../../Application/RoomAllocation/bingo-room-allocator');
import type {
  ZLinkEntrySpot,
  ZLinkEntrySpotContext
} from '../../../../../../../packages/framework/dist';
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
  readonly context?: ZLinkEntrySpotContext;

  constructor(private readonly roomDirectory: BingoRoomAllocatorType) {}

  async matchActor(actor: PlayerActorType, request: MatchBingoReq): Promise<MatchBingoRes> {
    const matched = await this.roomDirectory.allocate(request.mode);
    const joinRequest = createProtobufMessage(bingoRoomJoinReq(matched.roomId, actor.actorId, actor.displayName));
    try {
      const joined = await actor.context.joinSpot(matched.roomId, joinRequest).submit();
      const reply: BingoJoinReply = joined.reply === undefined ? {} : readProtobufMessage(joined.reply) as BingoJoinReply;
      joined.reply?.close();
      if (joined.resultCode !== 0) {
        throw new Error(reply.error ?? `Room ${matched.roomId} rejected actor '${actor.actorId}'.`);
      }
      return matchBingoRes(matched.roomId, reply.state);
    } finally {
      joinRequest.close();
    }
  }

  async onJoinActor(actor: PlayerActorType): Promise<void> {
    if (!actor.destroyAfterEntrySpotJoin) {
      return;
    }
    await this.context?.destroyActor(actor);
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
