type BingoRoomTimerTarget = {
  runTimerDraws(): Promise<unknown>;
};

class BingoRoomTimerHandler {
  [key: string]: any;
  async handle(room: BingoRoomTimerTarget): Promise<unknown> {
    return await room.runTimerDraws();
  }
}

export { BingoRoomTimerHandler };
