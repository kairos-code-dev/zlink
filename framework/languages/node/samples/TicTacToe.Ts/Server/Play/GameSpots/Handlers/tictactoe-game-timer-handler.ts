class TicTacToeGameTimerHandler {
  [key: string]: any;
  register(room) {
    room.timerRegistered = true;
    return { timerRegistered: true };
  }
}

export { TicTacToeGameTimerHandler };
