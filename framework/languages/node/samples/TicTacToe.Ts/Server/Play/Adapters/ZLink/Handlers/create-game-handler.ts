const { Inject } = require('@nestjs/common');
const { zlinkRequestHandler } = require('../../../../../../../../packages/nestjs/dist');
const { createGameRes } = require('../../../../../Shared/Contracts/messages');
const { PacketNames } = require('../../../../../Shared/Contracts/messages');
const { PLAY_STREAM_ENDPOINT } = require('../../../play-tokens');
const { TicTacToeGameTimerHandler } = require('../Spots/Handlers/tictactoe-game-timer-handler');
const { TicTacToeGameCreator } = require('../../../Application/GameCreation/tictactoe-game-creator');
import type {
  CreateGameReq,
  CreateGameRes
} from '../../../../../Shared/Contracts/messages';

class CreateGameHandler {
  [key: string]: any;
  constructor(games: any, playEndpoint: string, timerHandler: any) {
    this.games = games;
    this.playEndpoint = playEndpoint;
    this.timerHandler = timerHandler;
  }

  handle(request: CreateGameReq): CreateGameRes {
    const room = this.games.create(request.gameName ?? 'match', this.playEndpoint);
    this.timerHandler.register(room);
    return createGameRes(room.roomId, room.gameName, room.playEndpoint);
  }
}

Inject(TicTacToeGameCreator)(CreateGameHandler, undefined, 0);
Inject(PLAY_STREAM_ENDPOINT)(CreateGameHandler, undefined, 1);
Inject(TicTacToeGameTimerHandler)(CreateGameHandler, undefined, 2);
zlinkRequestHandler('play', PacketNames.createGame)(CreateGameHandler);

export { CreateGameHandler };
