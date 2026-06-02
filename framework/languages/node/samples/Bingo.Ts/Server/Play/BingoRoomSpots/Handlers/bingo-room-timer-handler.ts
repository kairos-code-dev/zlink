class BingoRoomTimerHandler {
  [key: string]: any;
  async handle(room) {
    return await room.runTimerDraws();
  }
}

export { BingoRoomTimerHandler };
