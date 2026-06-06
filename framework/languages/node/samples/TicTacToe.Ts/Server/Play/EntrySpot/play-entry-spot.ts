const { Inject } = require('@nestjs/common');
const { PlayActorJoinGameHandler } = require('./Handlers/play-actor-join-game-handler');

class PlayEntrySpot {
  [key: string]: any;
  constructor(joinHandler) {
    this.joinHandler = joinHandler;
  }

  join(actor, gameId) {
    return this.joinHandler.handle({ actor, gameId });
  }
}

Inject(PlayActorJoinGameHandler)(PlayEntrySpot, undefined, 0);

export { PlayEntrySpot };
