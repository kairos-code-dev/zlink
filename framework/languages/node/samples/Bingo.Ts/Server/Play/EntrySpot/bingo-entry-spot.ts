class BingoEntrySpot {
  [key: string]: any;
  constructor(roomDirectory, roomJoinHandler) {
    this.roomDirectory = roomDirectory;
    this.roomJoinHandler = roomJoinHandler;
  }

  async match(actor, request) {
    const matched = await this.roomDirectory.allocate(request.mode);
    const joined = await this.roomJoinHandler.handle(matched.room, actor, {
      roomId: matched.roomId,
      actorId: actor.actorId,
      displayName: actor.displayName
    });
    return {
      roomId: matched.roomId,
      state: joined.state
    };
  }
}

export { BingoEntrySpot };
