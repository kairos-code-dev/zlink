const { ZLinkHandlerGroup, ZLinkRequest } = require('../../../../../packages/framework/dist');

class AllocateBingoRoomHandler {
  constructor() {
    this.currentRoomId = null;
    this.currentMode = null;
    this.reservedSeats = 0;
    this.roomSeq = 0;
  }

  handle(request) {
    const requiredPlayers = 4;
    const mode = request.mode ?? 'four-player';
    if (this.currentRoomId === null || this.currentMode !== mode || this.reservedSeats >= requiredPlayers) {
      this.roomSeq += 1;
      this.currentRoomId = `bingo-room-${String(this.roomSeq).padStart(3, '0')}`;
      this.currentMode = mode;
      this.reservedSeats = 0;
    }

    this.reservedSeats += 1;
    return { roomId: this.currentRoomId };
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
