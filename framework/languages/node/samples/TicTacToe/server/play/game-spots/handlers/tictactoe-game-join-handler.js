class TicTacToeGameJoinHandler {
  handle(game, request) {
    game.join(request.actorId, request.mark);
    return { joined: true, matchId: game.matchId, actorId: request.actorId };
  }
}

module.exports = { TicTacToeGameJoinHandler };
