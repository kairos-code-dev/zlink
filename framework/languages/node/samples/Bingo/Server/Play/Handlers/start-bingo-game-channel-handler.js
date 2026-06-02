const { ZLinkHandlerGroup, ZLinkRequest } = require('../../../../../packages/framework/dist');

class StartBingoGameChannelHandler {
  constructor(actorFactory, rooms, startBingoGame) {
    this.actorFactory = actorFactory;
    this.rooms = rooms;
    this.startBingoGame = startBingoGame;
  }

  async handle(request) {
    const actor = await this.actorFactory.ensure(request.actorId, request.displayName);
    const room = this.rooms.require(request.roomId);
    try {
      return await this.startBingoGame.handle(room, actor, request);
    } catch (error) {
      return {
        rejected: true,
        reason: error.message
      };
    }
  }
}

ZLinkHandlerGroup('play')(StartBingoGameChannelHandler);
ZLinkRequest('StartBingoGameReq')(StartBingoGameChannelHandler.prototype, 'handle', descriptor());

module.exports = { StartBingoGameChannelHandler };

function descriptor() {
  return {
    configurable: true,
    enumerable: false,
    value: StartBingoGameChannelHandler.prototype.handle,
    writable: true
  };
}
