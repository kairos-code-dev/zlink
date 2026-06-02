const { ZLinkHandlerGroup, ZLinkRequest } = require('../../../../../packages/framework/dist');

class EnsurePlayerActorHandler {
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

ZLinkHandlerGroup('play')(EnsurePlayerActorHandler);
ZLinkRequest('EnsurePlayerActorReq')(EnsurePlayerActorHandler.prototype, 'handle', descriptor());

module.exports = { EnsurePlayerActorHandler };

function descriptor() {
  return {
    configurable: true,
    enumerable: false,
    value: EnsurePlayerActorHandler.prototype.handle,
    writable: true
  };
}
