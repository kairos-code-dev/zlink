class BingoRoomJoinHandler {
  [key: string]: any;
  async handle(room, actor, request) {
    return await room.join(actor, request);
  }
}

export { BingoRoomJoinHandler };
