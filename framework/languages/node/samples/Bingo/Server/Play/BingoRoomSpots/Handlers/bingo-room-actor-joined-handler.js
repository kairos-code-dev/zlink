class BingoRoomActorJoinedHandler {
  handle(room, actor) {
    return { roomId: room.roomId, actorId: actor.actorId };
  }
}

module.exports = { BingoRoomActorJoinedHandler };
