const { Inject, Injectable } = require('@nestjs/common');
const { PacketNames, gameStateNotify, placeMarkRes } = require('../../../../../../Shared/Contracts/messages');
const { TicTacToeGameCreator } = require('../../../../Application/GameCreation/tictactoe-game-creator');
import type { TicTacToeGameCreator as TicTacToeGameCreatorType } from '../../../../Application/GameCreation/tictactoe-game-creator';
import type {
  PlaceMarkInternalReq,
  PlaceMarkRes
} from '../../../../../../Shared/Contracts/messages';

@Injectable()
class PlayActorPlaceMarkHandler {
  constructor(@Inject(TicTacToeGameCreator) private readonly games: TicTacToeGameCreatorType) {}

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
    if (state.status === 'Won' || state.status === 'Draw' || state.status === 'TurnTimedOut') {
      setImmediate(() => {
        void this.games.cleanupFinishedRoom(room);
      });
    }
    return placeMarkRes(room.roomId, request.actor.actorId, request.cell, state);
  }
}

export { PlayActorPlaceMarkHandler };
