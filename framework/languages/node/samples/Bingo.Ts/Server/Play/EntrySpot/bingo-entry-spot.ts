const { Inject } = require('@nestjs/common');
const {
  bingoRoomJoinReq,
  createJsonMessage,
  matchBingoRes,
  readJsonMessage
} = require('../../../Shared/Contracts/messages');
const { BingoRoomDirectory } = require('../Handlers/bingo-room-directory');
import type {
  MatchBingoReq,
  MatchBingoRes,
  RoomJoinError,
  StateEnvelope
} from '../../../Shared/Contracts/messages';

type BingoActor = {
  actorId: string;
  displayName: string;
};

type BingoJoinReply = Partial<StateEnvelope & RoomJoinError>;

class BingoEntrySpot {
  [key: string]: any;
  constructor(roomDirectory: any) {
    this.roomDirectory = roomDirectory;
  }

  async match(actor: BingoActor, request: MatchBingoReq): Promise<MatchBingoRes> {
    const matched = await this.roomDirectory.allocate(request.mode);
    const joinRequest = createJsonMessage(bingoRoomJoinReq(matched.roomId, actor.actorId, actor.displayName));
    try {
      const joined = await matched.room.onActorJoin(actor, joinRequest);
      const reply: BingoJoinReply = joined.reply === undefined ? {} : readJsonMessage(joined.reply) as BingoJoinReply;
      joined.reply?.close();
      if (!joined.accepted) {
        throw new Error(reply.error ?? `Room ${matched.roomId} rejected actor '${actor.actorId}'.`);
      }
      return matchBingoRes(matched.roomId, reply.state);
    } finally {
      joinRequest.close();
    }
  }
}

Inject(BingoRoomDirectory)(BingoEntrySpot, undefined, 0);

export { BingoEntrySpot };
