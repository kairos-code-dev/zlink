type TimerRoom = {
  timerRegistered: boolean;
};

class TicTacToeGameTimerHandler {
  register(room: TimerRoom): { timerRegistered: boolean } {
    room.timerRegistered = true;
    return { timerRegistered: true };
  }
}

export { TicTacToeGameTimerHandler };
