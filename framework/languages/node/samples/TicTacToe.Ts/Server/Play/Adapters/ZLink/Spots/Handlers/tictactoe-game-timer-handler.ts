const { zlinkSpotTimerHandler } = require('../../../../../../../../../packages/nestjs/dist');

type TimerRoom = {
  timerRegistered: boolean;
};

@zlinkSpotTimerHandler()
class TicTacToeGameTimerHandler {
  register(room: TimerRoom): { timerRegistered: boolean } {
    room.timerRegistered = true;
    return { timerRegistered: true };
  }
}

export { TicTacToeGameTimerHandler };
