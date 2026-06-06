const { Inject } = require('@nestjs/common');
const { BingoRoomJoinHandler } = require('../BingoRoomSpots/Handlers/bingo-room-join-handler');
const { BingoRoomDirectory } = require('../Handlers/bingo-room-directory');

class BingoEntrySpot {
  constructor(roomDirectory, roomJoinHandler) {
    this.roomDirectory = roomDirectory;
    this.roomJoinHandler = roomJoinHandler;
  }

  async match(actor, request) {
    const matched = await this.roomDirectory.allocate(request.mode);
    const joined = await this.roomJoinHandler.handle(matched.room, actor, {
      roomId: matched.roomId,
      actorId: actor.actorId,
      displayName: actor.displayName
    });
    return {
      roomId: matched.roomId,
      state: joined.state
    };
  }
}

Inject(BingoRoomDirectory)(BingoEntrySpot, undefined, 0);
Inject(BingoRoomJoinHandler)(BingoEntrySpot, undefined, 1);

module.exports = { BingoEntrySpot };
