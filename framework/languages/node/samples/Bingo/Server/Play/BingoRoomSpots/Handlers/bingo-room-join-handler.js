class BingoRoomJoinHandler {
  async handle(room, actor, request) {
    return await room.join(actor, request);
  }
}

module.exports = { BingoRoomJoinHandler };
