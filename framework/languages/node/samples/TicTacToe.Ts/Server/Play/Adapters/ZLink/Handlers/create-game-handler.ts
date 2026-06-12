const { Inject } = require('@nestjs/common');
const { zlinkRequestHandler } = require('../../../../../../../../packages/nestjs/dist');
const { createGameRes } = require('../../../../../Shared/Contracts/messages');
const { PacketNames } = require('../../../../../Shared/Contracts/messages');
const { PLAY_STREAM_ENDPOINT } = require('../../../play-tokens');
const { TicTacToeGameTimerHandler } = require('../Spots/Handlers/tictactoe-game-timer-handler');
const { TicTacToeGameCreator } = require('../../../Application/GameCreation/tictactoe-game-creator');
import type { ZLinkRequestHandler } from '../../../../../../../packages/framework/dist';
import type { TicTacToeGameCreator as TicTacToeGameCreatorType } from '../../../Application/GameCreation/tictactoe-game-creator';
import type { TicTacToeGameTimerHandler as TicTacToeGameTimerHandlerType } from '../Spots/Handlers/tictactoe-game-timer-handler';
import type {
  CreateGameReq,
  CreateGameRes
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('play', PacketNames.createGame)
class CreateGameHandler implements ZLinkRequestHandler<CreateGameReq, CreateGameRes> {
  constructor(
    @Inject(TicTacToeGameCreator) private readonly games: TicTacToeGameCreatorType,
    @Inject(PLAY_STREAM_ENDPOINT) private readonly playEndpoint: string,
    @Inject(TicTacToeGameTimerHandler) private readonly timerHandler: TicTacToeGameTimerHandlerType
  ) {}

  async handle(request: CreateGameReq): Promise<CreateGameRes> {
    const room = this.games.create(request.gameName ?? 'match', this.playEndpoint);
    this.timerHandler.register(room);
    return createGameRes(room.roomId, room.gameName, room.playEndpoint);
  }
}

export { CreateGameHandler };
