type BingoRoomTimerTarget = {
  runTimerDraws(): Promise<unknown>;
};

class BingoRoomTimerHandler {
  async handle(room: BingoRoomTimerTarget): Promise<unknown> {
    return await room.runTimerDraws();
  }
}

export { BingoRoomTimerHandler };
