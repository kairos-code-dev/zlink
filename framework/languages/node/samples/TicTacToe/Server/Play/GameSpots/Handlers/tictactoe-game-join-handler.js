class TicTacToeGameJoinHandler {
  handle(room, actor) {
    return room.addPlayer(actor);
  }
}

module.exports = { TicTacToeGameJoinHandler };
