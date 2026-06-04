class TicTacToeGameTimerHandler {
  register(room) {
    room.timerRegistered = true;
    return { timerRegistered: true };
  }
}

module.exports = { TicTacToeGameTimerHandler };
