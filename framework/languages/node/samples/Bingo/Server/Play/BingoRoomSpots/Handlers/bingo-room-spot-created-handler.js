class BingoRoomSpotCreatedHandler {
  handle(room, settings) {
    return { roomId: room.roomId, mode: settings.mode };
  }
}

module.exports = { BingoRoomSpotCreatedHandler };
