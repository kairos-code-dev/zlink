const { Inject } = require('@nestjs/common');
const { ZLINK_CHANNEL_CLIENT } = require('../../../../../../packages/nestjs/dist');
const { PacketNames, SampleNames, SampleTimings } = require('../../../Shared/Contracts/messages');

class CreateGameHttpHandler {
  [key: string]: any;
  constructor(playClient) {
    this.playClient = playClient;
  }

  async handle(request) {
    const response = await this.playClient
      .requestToChannel(SampleNames.playChannel, {
        gameName: request.gameName
      })
      .packetName(PacketNames.createGame)
      .timeout(SampleTimings.requestTimeout)
      .submit();
    return {
      gameId: response.gameId,
      gameName: response.gameName,
      playEndpoint: response.playEndpoint
    };
  }
}

Inject(ZLINK_CHANNEL_CLIENT)(CreateGameHttpHandler, undefined, 0);

export { CreateGameHttpHandler };
