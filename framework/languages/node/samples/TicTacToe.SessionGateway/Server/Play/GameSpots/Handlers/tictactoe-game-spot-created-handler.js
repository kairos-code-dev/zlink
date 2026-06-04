class TicTacToeGameSpotCreatedHandler {
  handle(room) {
    return { matchId: room.matchId, created: true };
  }
}

module.exports = { TicTacToeGameSpotCreatedHandler };
