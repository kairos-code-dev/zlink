type TimerRoom = {
  timerRegistered: boolean;
};

class TicTacToeGameTimerHandler {
  [key: string]: any;
  register(room: TimerRoom): { timerRegistered: boolean } {
    room.timerRegistered = true;
    return { timerRegistered: true };
  }
}

export { TicTacToeGameTimerHandler };
