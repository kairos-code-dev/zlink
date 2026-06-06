const { Inject } = require('@nestjs/common');
const { PLAY_STREAM_ENDPOINT } = require('../play-tokens');
const { TicTacToeGameCreatedHandler } = require('../GameSpots/Handlers/tictactoe-game-created-handler');
const { TicTacToeGameTimerHandler } = require('../GameSpots/Handlers/tictactoe-game-timer-handler');
const { TicTacToeGameDirectory } = require('../GameSpots/tictactoe-game');

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

Inject(TicTacToeGameDirectory)(CreateGameHandler, undefined, 0);
Inject(PLAY_STREAM_ENDPOINT)(CreateGameHandler, undefined, 1);
Inject(TicTacToeGameCreatedHandler)(CreateGameHandler, undefined, 2);
Inject(TicTacToeGameTimerHandler)(CreateGameHandler, undefined, 3);

module.exports = { CreateGameHandler };
