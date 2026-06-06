const { Inject } = require('@nestjs/common');
const { createJsonMessage } = require('../../../Shared/Contracts/messages');
const { PLAY_STREAM_ENDPOINT } = require('../play-tokens');
const { TicTacToeGameTimerHandler } = require('../GameSpots/Handlers/tictactoe-game-timer-handler');
const { TicTacToeGameDirectory } = require('../GameSpots/tictactoe-game');

class CreateGameHandler {
  [key: string]: any;
  constructor(games, playEndpoint, timerHandler) {
    this.games = games;
    this.playEndpoint = playEndpoint;
    this.timerHandler = timerHandler;
  }

  handle(request) {
    const room = this.games.create(request.gameName ?? 'match', this.playEndpoint);
    const createRequest = createJsonMessage({ gameName: room.gameName });
    try {
      const created = room.onCreate(createRequest);
      if (created.accepted === false) {
        throw new Error(`Game '${room.gameId}' creation was rejected.`);
      }
    } finally {
      createRequest.close();
    }
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
Inject(TicTacToeGameTimerHandler)(CreateGameHandler, undefined, 2);

export { CreateGameHandler };
