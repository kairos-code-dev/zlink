const { Inject } = require('@nestjs/common');
const {
  PacketNames,
  joinGameRes,
  playerJoinedNotify
} = require('../../../../../../Shared/Contracts/messages');
const { TicTacToeGameDirectory } = require('../../../../Application/GameCreation/tictactoe-game');
import type {
  JoinGameInternalReq,
  JoinGameRes
} from '../../../../../../Shared/Contracts/messages';

class PlayActorJoinGameHandler {
  [key: string]: any;
  constructor(games: any) {
    this.games = games;
  }

  handle(request: JoinGameInternalReq): JoinGameRes {
    const room = this.games.require(request.roomId);
    const result = room.addPlayer(request.actor);
    (request.actor as any).roomId = room.roomId;
    const state = room.state();
    if (result.newlyJoined && room.players.size === 2) {
      for (const player of room.players.values()) {
        if (player.actorId === result.joined.actorId) {
          continue;
        }
        player.actor.push(
          PacketNames.playerJoinedNotify,
          playerJoinedNotify(room.roomId, result.joined.actorId, result.joined.mark, state)
        );
        player.actor.push(PacketNames.gameStateNotify, state);
      }
    }
    return joinGameRes(room.roomId, result.joined.actorId, result.joined.mark, state);
  }
}

Inject(TicTacToeGameDirectory)(PlayActorJoinGameHandler, undefined, 0);

export { PlayActorJoinGameHandler };
