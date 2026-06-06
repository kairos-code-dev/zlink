const { Inject } = require('@nestjs/common');
const { ZLINK_CHANNEL_CLIENT } = require('../../../../../packages/nestjs/dist');

class MatchBingoHandler {
  constructor(zlinkClient) {
    this.zlinkClient = zlinkClient;
  }

  async handle(request) {
    const allocated = await this.zlinkClient
      .requestToChannel('bingo.play', {
        mode: request.mode ?? 'four-player'
      })
      .packetName('AllocateBingoRoom')
      .timeout(10000)
      .submit();
    return { roomId: allocated.roomId };
  }
}

Inject(ZLINK_CHANNEL_CLIENT)(MatchBingoHandler, undefined, 0);

module.exports = { MatchBingoHandler };
