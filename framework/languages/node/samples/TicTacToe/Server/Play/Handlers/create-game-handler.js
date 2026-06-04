class CreateGameHandler {
  constructor(games, playEndpoint, createdHandler, timerHandler) {
    this.games = games;
    this.playEndpoint = playEndpoint;
    this.createdHandler = createdHandler;
    this.timerHandler = timerHandler;
  }

  handle(request) {
    const room = this.games.create(request.gameName ?? 'match', this.playEndpoint);
    this.createdHandler.handle(room);
    this.timerHandler.register(room);
    return {
      gameId: room.gameId,
      gameName: room.gameName,
      playEndpoint: room.playEndpoint
    };
  }
}

module.exports = { CreateGameHandler };
