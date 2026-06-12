const { zlinkSpotTimerHandler } = require('../../../../../../../../../packages/nestjs/dist');

type BingoRoomTimerTarget = {
  runTimerDraws(): Promise<unknown>;
};

@zlinkSpotTimerHandler()
class BingoRoomTimerHandler {
  async handle(room: BingoRoomTimerTarget): Promise<unknown> {
    return await room.runTimerDraws();
  }
}

export { BingoRoomTimerHandler };
