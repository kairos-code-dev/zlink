const { Inject } = require('@nestjs/common');
const {
  PacketNames,
  createJsonMessage,
  joinGameReq,
  joinGameRes,
  playerJoinedNotify,
  readJsonMessage
} = require('../../../../Shared/Contracts/messages');
const { TicTacToeGameDirectory } = require('../../GameSpots/tictactoe-game');
import type {
  JoinGameInternalReq,
  JoinGameRes
} from '../../../../Shared/Contracts/messages';

type JoinReply = {
  error?: string;
  actorId: string;
  mark: string;
};

class PlayActorJoinGameHandler {
  [key: string]: any;
  constructor(games: any) {
    this.games = games;
  }

  handle(request: JoinGameInternalReq): JoinGameRes {
    const room = this.games.require(request.gameId);
    const joinRequest = createJsonMessage(joinGameReq(request.gameId));
    const result = room.onActorJoin(request.actor, joinRequest);
    const reply: Partial<JoinReply> = result.reply === undefined ? {} : readJsonMessage(result.reply) as JoinReply;
    joinRequest.close();
    result.reply?.close();
    if (!result.accepted) {
      throw new Error(reply.error ?? `Game '${request.gameId}' rejected actor '${request.actor.actorId}'.`);
    }
    const joined = reply;
    for (const player of room.players.values()) {
      player.actor.push(
        PacketNames.playerJoinedNotify,
        playerJoinedNotify(room.gameId, joined.actorId, [...room.players.values()].map((value) => value.actorId))
      );
    }
    return joinGameRes(room.gameId, joined.actorId, joined.mark, room.state());
  }
}

Inject(TicTacToeGameDirectory)(PlayActorJoinGameHandler, undefined, 0);

export { PlayActorJoinGameHandler };
