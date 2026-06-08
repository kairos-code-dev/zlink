const { Inject } = require('@nestjs/common');
const { PacketNames, placeMarkRes } = require('../../../../Shared/Contracts/messages');
const { TicTacToeGameDirectory } = require('../tictactoe-game');
import type {
  PlaceMarkReq,
  PlaceMarkRes
} from '../../../../Shared/Contracts/messages';

class PlayActorPlaceMarkHandler {
  [key: string]: any;
  constructor(games: any) {
    this.games = games;
  }

  handle(request: PlaceMarkReq): PlaceMarkRes {
    const room = this.games.require(request.gameId);
    const state = room.place(request.actor.actorId, request.cell);
    for (const joined of room.players.values()) {
      joined.actor.push(PacketNames.gameStateNotify, state);
    }
    return placeMarkRes(room.gameId, request.actor.actorId, request.cell, state);
  }
}

Inject(TicTacToeGameDirectory)(PlayActorPlaceMarkHandler, undefined, 0);

export { PlayActorPlaceMarkHandler };
