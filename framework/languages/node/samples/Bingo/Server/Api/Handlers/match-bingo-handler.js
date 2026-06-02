const { ZLinkHandlerGroup, ZLinkRequest } = require('../../../../../packages/framework/dist');

class MatchBingoHandler {
  constructor(playClient) {
    this.playClient = playClient;
  }

  async handle(request) {
    const allocated = await this.playClient
      .requestToChannel('bingo.play', {
        mode: request.mode ?? 'four-player'
      })
      .packetName('AllocateBingoRoom')
      .timeout(10000)
      .submit();
    return { roomId: allocated.roomId };
  }
}

ZLinkHandlerGroup('api')(MatchBingoHandler);
ZLinkRequest('MatchBingoApiReq')(MatchBingoHandler.prototype, 'handle', descriptor());

module.exports = { MatchBingoHandler };

function descriptor() {
  return {
    configurable: true,
    enumerable: false,
    value: MatchBingoHandler.prototype.handle,
    writable: true
  };
}
