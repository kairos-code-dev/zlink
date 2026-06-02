const { ZLinkHandlerGroup, ZLinkRequest } = require('../../../../../packages/framework/dist');

class RunBingoRoomTimerHandler {
  constructor(rooms, timer) {
    this.rooms = rooms;
    this.timer = timer;
  }

  async handle(request) {
    const room = this.rooms.require(request.roomId);
    const state = await this.timer.handle(room);
    return { state };
  }
}

ZLinkHandlerGroup('play')(RunBingoRoomTimerHandler);
ZLinkRequest('RunBingoRoomTimerReq')(RunBingoRoomTimerHandler.prototype, 'handle', descriptor());

module.exports = { RunBingoRoomTimerHandler };

function descriptor() {
  return {
    configurable: true,
    enumerable: false,
    value: RunBingoRoomTimerHandler.prototype.handle,
    writable: true
  };
}
