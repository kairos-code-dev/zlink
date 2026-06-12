const { Inject } = require('@nestjs/common');
const { PacketNames, gameStateNotify, placeMarkRes } = require('../../../../../../Shared/Contracts/messages');
const { TicTacToeGameCreator } = require('../../../../Application/GameCreation/tictactoe-game-creator');
import type { TicTacToeGameCreator as TicTacToeGameCreatorType } from '../../../../Application/GameCreation/tictactoe-game-creator';
import type {
  PlaceMarkInternalReq,
  PlaceMarkRes
} from '../../../../../../Shared/Contracts/messages';

class PlayActorPlaceMarkHandler {
  constructor(private readonly games: TicTacToeGameCreatorType) {}

  async handle(request: PlaceMarkInternalReq): Promise<PlaceMarkRes> {
    if (request.actor.roomId === undefined) {
      throw new Error(`Actor '${request.actor.actorId}' has not joined a room.`);
    }
    const room = this.games.require(request.actor.roomId);
    const change = room.match.placeMark(request.actor.actorId, request.cell);
    const state = change.state;
    for (const joined of room.match.players.values()) {
      if (joined.actorId === request.actor.actorId) {
        continue;
      }
      await joined.actor.push(PacketNames.gameStateNotify, gameStateNotify(state));
    }
    return placeMarkRes(room.roomId, request.actor.actorId, request.cell, state);
  }
}

Inject(TicTacToeGameCreator)(PlayActorPlaceMarkHandler, undefined, 0);

export { PlayActorPlaceMarkHandler };
