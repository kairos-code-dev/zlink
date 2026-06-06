const { Inject } = require('@nestjs/common');
const { createJsonMessage, readJsonMessage } = require('../../../Shared/Contracts/messages');
const { BingoRoomDirectory } = require('../Handlers/bingo-room-directory');

class BingoEntrySpot {
  [key: string]: any;
  constructor(roomDirectory) {
    this.roomDirectory = roomDirectory;
  }

  async match(actor, request) {
    const matched = await this.roomDirectory.allocate(request.mode);
    const joinRequest = createJsonMessage({
      roomId: matched.roomId,
      actorId: actor.actorId,
      displayName: actor.displayName
    });
    try {
      const joined = await matched.room.onActorJoin(actor, joinRequest);
      const reply = joined.reply === undefined ? {} : readJsonMessage(joined.reply);
      joined.reply?.close();
      if (!joined.accepted) {
        throw new Error(reply.error ?? `Room ${matched.roomId} rejected actor '${actor.actorId}'.`);
      }
      return {
        roomId: matched.roomId,
        state: reply.state
      };
    } finally {
      joinRequest.close();
    }
  }
}

Inject(BingoRoomDirectory)(BingoEntrySpot, undefined, 0);

export { BingoEntrySpot };
