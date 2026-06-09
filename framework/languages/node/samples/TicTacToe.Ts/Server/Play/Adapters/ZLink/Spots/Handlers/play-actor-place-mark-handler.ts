const { Inject } = require('@nestjs/common');
const { PacketNames, gameStateNotify, placeMarkRes } = require('../../../../../../Shared/Contracts/messages');
const { TicTacToeGameDirectory } = require('../../../../Application/GameCreation/tictactoe-game');
import type {
  PlaceMarkInternalReq,
  PlaceMarkRes
} from '../../../../../../Shared/Contracts/messages';

class PlayActorPlaceMarkHandler {
  [key: string]: any;
  constructor(games: any) {
    this.games = games;
  }

  handle(request: PlaceMarkInternalReq): PlaceMarkRes {
    const room = this.games.require((request.actor as any).roomId);
    const change = room.match.placeMark(request.actor.actorId, request.cell);
    const state = change.state;
    for (const joined of room.match.players.values()) {
      if (joined.actorId === request.actor.actorId) {
        continue;
      }
      joined.actor.push(PacketNames.gameStateNotify, gameStateNotify(state));
    }
    return placeMarkRes(room.roomId, request.actor.actorId, request.cell, state);
  }
}

Inject(TicTacToeGameDirectory)(PlayActorPlaceMarkHandler, undefined, 0);

export { PlayActorPlaceMarkHandler };
