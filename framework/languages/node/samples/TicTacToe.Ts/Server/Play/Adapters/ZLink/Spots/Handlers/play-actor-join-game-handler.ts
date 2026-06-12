const { Inject } = require('@nestjs/common');
const {
  PacketNames,
  gameStateNotify,
  joinGameRes,
  playerJoinedNotify
} = require('../../../../../../Shared/Contracts/messages');
const { TicTacToeGameCreator } = require('../../../../Application/GameCreation/tictactoe-game-creator');
import type { TicTacToeGameCreator as TicTacToeGameCreatorType } from '../../../../Application/GameCreation/tictactoe-game-creator';
import type {
  JoinGameInternalReq,
  JoinGameRes
} from '../../../../../../Shared/Contracts/messages';

class PlayActorJoinGameHandler {
  constructor(private readonly games: TicTacToeGameCreatorType) {}

  async handle(request: JoinGameInternalReq): Promise<JoinGameRes> {
    const room = this.games.require(request.roomId);
    const result = room.match.joinPlayer(request.actor);
    request.actor.roomId = room.roomId;
    const state = result.state;
    if (result.newlyJoined && room.match.players.size === 2) {
      for (const player of room.match.players.values()) {
        if (player.actorId === result.joined.actorId) {
          continue;
        }
        await player.actor.push(
          PacketNames.playerJoinedNotify,
          playerJoinedNotify(room.roomId, result.joined.actorId, result.joined.mark, state)
        );
        await player.actor.push(PacketNames.gameStateNotify, gameStateNotify(state));
      }
    }
    return joinGameRes(room.roomId, result.joined.actorId, result.joined.mark, state);
  }
}

Inject(TicTacToeGameCreator)(PlayActorJoinGameHandler, undefined, 0);

export { PlayActorJoinGameHandler };
