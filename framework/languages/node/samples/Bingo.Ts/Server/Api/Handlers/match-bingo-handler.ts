const { Inject } = require('@nestjs/common');
const { ZLINK_CHANNEL_CLIENT } = require('../../../../../../packages/nestjs/dist');

class MatchBingoHandler {
  [key: string]: any;

  constructor(@Inject(ZLINK_CHANNEL_CLIENT) zlinkClient) {
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

export { MatchBingoHandler };
