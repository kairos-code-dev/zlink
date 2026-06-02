class BingoRoomActorJoinedHandler {
  [key: string]: any;
  handle(room, actor) {
    return { roomId: room.roomId, actorId: actor.actorId };
  }
}

export { BingoRoomActorJoinedHandler };
