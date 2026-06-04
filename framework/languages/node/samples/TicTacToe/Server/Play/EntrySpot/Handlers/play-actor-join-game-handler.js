const { PacketNames } = require('../../../../Shared/Contracts/messages');

class PlayActorJoinGameHandler {
  constructor(games, gameJoinHandler) {
    this.games = games;
    this.gameJoinHandler = gameJoinHandler;
  }

  handle(request) {
    const room = this.games.require(request.gameId);
    const joined = this.gameJoinHandler.handle(room, request.actor);
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

module.exports = { PlayActorJoinGameHandler };
