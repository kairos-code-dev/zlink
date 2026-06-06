const { Inject } = require('@nestjs/common');
const { BingoRoomDirectory } = require('./bingo-room-directory');

class AllocateBingoRoomHandler {
  [key: string]: any;
  constructor(rooms) {
    this.rooms = rooms;
  }
  async handle(request) {
    const allocated = await this.rooms.allocate(request.mode ?? 'four-player');
    return { roomId: allocated.roomId };
  }
}

Inject(BingoRoomDirectory)(AllocateBingoRoomHandler, undefined, 0);

export { AllocateBingoRoomHandler };
