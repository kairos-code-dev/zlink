const { ZLinkHandlerGroup, ZLinkRequest } = require('../../../../../../packages/framework/dist');

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

ZLinkHandlerGroup('play')(AllocateBingoRoomHandler);
ZLinkRequest('AllocateBingoRoom')(AllocateBingoRoomHandler.prototype, 'handle', descriptor());

export { AllocateBingoRoomHandler };

function descriptor() {
  return {
    configurable: true,
    enumerable: false,
    value: AllocateBingoRoomHandler.prototype.handle,
    writable: true
  };
}
