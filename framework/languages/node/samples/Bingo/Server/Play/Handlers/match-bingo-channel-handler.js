const { ZLinkHandlerGroup, ZLinkRequest } = require('../../../../../packages/framework/dist');

class MatchBingoChannelHandler {
  constructor(actorFactory, entrySpot, matchBingoActor) {
    this.actorFactory = actorFactory;
    this.entrySpot = entrySpot;
    this.matchBingoActor = matchBingoActor;
  }

  async handle(request) {
    const actor = await this.actorFactory.ensure(request.actorId, request.displayName);
    return await this.matchBingoActor.handle(this.entrySpot, actor, request);
  }
}

ZLinkHandlerGroup('play')(MatchBingoChannelHandler);
ZLinkRequest('MatchBingoReq')(MatchBingoChannelHandler.prototype, 'handle', descriptor());

module.exports = { MatchBingoChannelHandler };

function descriptor() {
  return {
    configurable: true,
    enumerable: false,
    value: MatchBingoChannelHandler.prototype.handle,
    writable: true
  };
}
