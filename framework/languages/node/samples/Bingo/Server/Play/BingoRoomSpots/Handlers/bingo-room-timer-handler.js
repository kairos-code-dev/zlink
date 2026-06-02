class BingoRoomTimerHandler {
  async handle(room) {
    return await room.runTimerDraws();
  }
}

module.exports = { BingoRoomTimerHandler };
