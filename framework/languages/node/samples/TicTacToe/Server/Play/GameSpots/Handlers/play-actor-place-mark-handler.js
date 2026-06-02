class PlayActorPlaceMarkHandler {
  handle(game, request) {
    const winner = game.place(request.actorId, request.cell);
    return {
      matchId: game.matchId,
      board: game.snapshot(),
      winner: winner ?? null
    };
  }
}

module.exports = { PlayActorPlaceMarkHandler };
