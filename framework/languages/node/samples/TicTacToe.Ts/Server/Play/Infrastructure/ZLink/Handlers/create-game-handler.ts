import { Inject } from '@nestjs/common';
import { TicTacToeGameCreator } from '../../../Application/GameCreation/tictactoe-game-creator';
import type {
  ZLinkRequestHandler
} from '@zlink-systems/framework';
import type { TicTacToeGameCreator as TicTacToeGameCreatorType } from '../../../Application/GameCreation/tictactoe-game-creator';
import type {
  CreateGameReq,
  CreateGameRes
} from '../../../../../Shared/Contracts/messages';

class CreateGameHandler implements ZLinkRequestHandler<CreateGameReq, CreateGameRes> {
  constructor(
    @Inject(TicTacToeGameCreator) private readonly games: TicTacToeGameCreatorType
  ) {}

  async handle(request: CreateGameReq): Promise<CreateGameRes> {
    return await this.games.create(request.gameName);
  }
}

export { CreateGameHandler };
