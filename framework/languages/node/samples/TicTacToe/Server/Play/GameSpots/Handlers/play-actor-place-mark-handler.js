const { PacketNames } = require('../../../../Shared/Contracts/messages');

class PlayActorPlaceMarkHandler {
  constructor(games) {
    this.games = games;
  }

  handle(request) {
    const room = this.games.require(request.gameId);
    const state = room.place(request.actor.actorId, request.cell);
    for (const joined of room.players.values()) {
      joined.actor.push(PacketNames.gameStateNotify, state);
    }
    return {
      gameId: room.gameId,
      actorId: request.actor.actorId,
      cell: request.cell,
      state
    };
  }
}

module.exports = { PlayActorPlaceMarkHandler };
