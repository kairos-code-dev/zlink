class BingoRoomSpotCreatedHandler {
  [key: string]: any;
  handle(room, settings) {
    return { roomId: room.roomId, mode: settings.mode };
  }
}

export { BingoRoomSpotCreatedHandler };
