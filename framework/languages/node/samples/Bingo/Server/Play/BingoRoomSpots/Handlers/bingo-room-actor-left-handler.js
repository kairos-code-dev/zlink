class BingoRoomActorLeftHandler {
  handle(room, actor) {
    return { roomId: room.roomId, actorId: actor.actorId };
  }
}

module.exports = { BingoRoomActorLeftHandler };
