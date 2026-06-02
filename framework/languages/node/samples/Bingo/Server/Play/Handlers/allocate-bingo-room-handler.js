const { ZLinkHandlerGroup, ZLinkRequest } = require('../../../../../packages/framework/dist');

class AllocateBingoRoomHandler {
  constructor(rooms) {
    this.rooms = rooms;
  }

  async handle(request) {
    const allocated = await this.rooms.allocate(request.mode ?? 'four-player');
    return { roomId: allocated.roomId };
  }
}

ZLinkHandlerGroup('play')(AllocateBingoRoomHandler);
ZLinkRequest('AllocateBingoRoom')(AllocateBingoRoomHandler.prototype, 'handle', descriptor());

module.exports = { AllocateBingoRoomHandler };

function descriptor() {
  return {
    configurable: true,
    enumerable: false,
    value: AllocateBingoRoomHandler.prototype.handle,
    writable: true
  };
}
