const { Inject } = require('@nestjs/common');
const {
  PacketNames,
  bingoRoomJoinReq,
  createProtobufMessage,
  matchBingoRes,
  readProtobufMessage
} = require('../../../../../Shared/Contracts/messages');
const { BingoRoomAllocator } = require('../../../Application/RoomAllocation/bingo-room-allocator');
const { MatchBingoActorHandler } = require('./Handlers/match-bingo-actor-handler');
const { PlayerActor } = require('../Actors/player-actor');
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

type BingoActor = {
  actorId: string;
  displayName: string;
};

type BingoJoinReply = Partial<StateEnvelope & RoomJoinError>;

class BingoEntrySpot implements ZLinkEntrySpot<PlayerActorType> {
  readonly context?: ZLinkEntrySpotContext;

  constructor(private readonly roomDirectory: BingoRoomAllocatorType) {}

  configure(): void {
    this.context?.handlers.addActorPacket(MatchBingoActorHandler, PlayerActor, PacketNames.matchBingoReq);
  }

  async matchActor(actor: PlayerActorType, request: MatchBingoReq): Promise<MatchBingoRes> {
    const matched = await this.roomDirectory.allocate(request.mode);
    const joinRequest = createProtobufMessage(bingoRoomJoinReq(matched.roomId, actor.actorId, actor.displayName));
    try {
      const joined = await matched.room.onActorJoin(actor, joinRequest);
      const reply: BingoJoinReply = joined.reply === undefined ? {} : readProtobufMessage(joined.reply) as BingoJoinReply;
      joined.reply?.close();
      if (!joined.accepted) {
        throw new Error(reply.error ?? `Room ${matched.roomId} rejected actor '${actor.actorId}'.`);
      }
      return matchBingoRes(matched.roomId, reply.state);
    } finally {
      joinRequest.close();
    }
  }

  async onPostActorJoined(actor: PlayerActorType): Promise<void> {
    void actor;
  }

  async onActorLeft(actor: PlayerActorType): Promise<void> {
    void actor;
  }

  async onActorDisconnected(actor: PlayerActorType): Promise<void> {
    void actor;
  }
}

Inject(BingoRoomAllocator)(BingoEntrySpot, undefined, 0);

export { BingoEntrySpot };
