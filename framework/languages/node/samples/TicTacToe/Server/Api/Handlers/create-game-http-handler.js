const { PacketNames, SampleNames, SampleTimings } = require('../../../Shared/Contracts/messages');

class CreateGameHttpHandler {
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

module.exports = { CreateGameHttpHandler };
