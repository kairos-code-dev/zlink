const { Inject } = require('@nestjs/common');
const { createJsonMessage, PacketNames, readJsonMessage } = require('../../../../Shared/Contracts/messages');
const { TicTacToeGameDirectory } = require('../../GameSpots/tictactoe-game');

class PlayActorJoinGameHandler {
  [key: string]: any;
  constructor(games) {
    this.games = games;
  }

  handle(request) {
    const room = this.games.require(request.gameId);
    const joinRequest = createJsonMessage({ gameId: request.gameId });
    const result = room.onActorJoin(request.actor, joinRequest);
    const reply = result.reply === undefined ? {} : readJsonMessage(result.reply);
    joinRequest.close();
    result.reply?.close();
    if (!result.accepted) {
      throw new Error(reply.error ?? `Game '${request.gameId}' rejected actor '${request.actor.actorId}'.`);
    }
    const joined = reply;
    for (const player of room.players.values()) {
      player.actor.push(PacketNames.playerJoinedNotify, {
        gameId: room.gameId,
        actorId: joined.actorId,
        players: [...room.players.values()].map((value) => value.actorId)
      });
    }
    return {
      gameId: room.gameId,
      actorId: joined.actorId,
      mark: joined.mark,
      state: room.state()
    };
  }
}

Inject(TicTacToeGameDirectory)(PlayActorJoinGameHandler, undefined, 0);

export { PlayActorJoinGameHandler };
