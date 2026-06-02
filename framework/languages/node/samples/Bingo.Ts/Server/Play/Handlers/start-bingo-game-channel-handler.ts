const { ZLinkHandlerGroup, ZLinkRequest } = require('../../../../../../packages/framework/dist');

class StartBingoGameChannelHandler {
  [key: string]: any;
  constructor(actorFactory, rooms, startBingoGame, timer) {
    this.actorFactory = actorFactory;
    this.rooms = rooms;
    this.startBingoGame = startBingoGame;
    this.timer = timer;
  }

  async handle(request) {
    const actor = await this.actorFactory.ensure(request.actorId, request.displayName);
    const room = this.rooms.require(request.roomId);
    try {
      const started = await this.startBingoGame.handle(room, actor, request);
      if (started.state.status === 'Running') {
        await this.timer.handle(room);
      }
      return started;
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

export { StartBingoGameChannelHandler };

function descriptor() {
  return {
    configurable: true,
    enumerable: false,
    value: StartBingoGameChannelHandler.prototype.handle,
    writable: true
  };
}
