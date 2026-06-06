const { Inject } = require('@nestjs/common');
const { PlayerActorFactory } = require('../Actors/player-actor-factory');

class EnsurePlayerActorHandler {
  [key: string]: any;
  constructor(actorFactory) {
    this.actorFactory = actorFactory;
  }
  async handle(request) {
    const actor = await this.actorFactory.ensure(request.actorId, request.displayName);
    return {
      actorId: actor.actorId,
      actorType: 'bingo.player',
      actor: {
        nodeRid: 'bingo.room.node',
        actorId: actor.actorId,
        generation: 1
      }
    };
  }
}

Inject(PlayerActorFactory)(EnsurePlayerActorHandler, undefined, 0);

export { EnsurePlayerActorHandler };
