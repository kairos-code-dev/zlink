const { Inject } = require('@nestjs/common');
const { ZLINK_SPOT_MANAGER, zlinkRequestHandler } = require('../../../../../../../../packages/nestjs/dist');
const { createGameRes } = require('../../../../../Shared/Contracts/messages');
const { PacketNames } = require('../../../../../Shared/Contracts/messages');
const { PLAY_STREAM_ENDPOINT } = require('../../../play-tokens');
const { TicTacToeGameTimerHandler } = require('../Spots/Handlers/tictactoe-game-timer-handler');
const { TicTacToeGameCreator } = require('../../../Application/GameCreation/tictactoe-game-creator');
const { TicTacToeGameSpot } = require('../Spots/tictactoe-game-spot');
import type {
  ZLinkRequestHandler,
  ZLinkSpotManager
} from '../../../../../../../packages/framework/dist';
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
    @Inject(TicTacToeGameTimerHandler) private readonly timerHandler: TicTacToeGameTimerHandlerType,
    @Inject(ZLINK_SPOT_MANAGER) private readonly spotManager: ZLinkSpotManager
  ) {}

  async handle(request: CreateGameReq): Promise<CreateGameRes> {
    const room = this.games.create(request.gameName ?? 'match', this.playEndpoint);
    await this.spotManager.getOrCreate(TicTacToeGameSpot, room.roomId);
    await this.spotManager.executeOnSpot<InstanceType<typeof TicTacToeGameSpot>, void>(
      TicTacToeGameSpot,
      room.roomId,
      (spot) => {
      spot.configureRoom(room);
      }
    );
    this.timerHandler.register(room);
    return createGameRes(room.roomId, room.gameName, room.playEndpoint);
  }
}

export { CreateGameHandler };
