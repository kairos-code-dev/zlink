const { PacketNames } = require('../../../../Shared/Contracts/messages');
const { SampleNames, SampleTimings } = require('../../../../Shared/Configuration/sample-names');

class CreateMatchSessionPacketHandler {
  constructor(apiClient, playClient) {
    this.apiClient = apiClient;
    this.playClient = playClient;
  }

  async handle(request, session) {
    const match = await this.apiClient.request(
      SampleNames.apiNode,
      PacketNames.createMatchReq,
      { matchName: request.matchName },
      SampleTimings.requestTimeout
    );
    const join = await this.playClient.request(
      SampleNames.playNode,
      PacketNames.joinMatchReq,
      { matchId: match.matchId, actorId: session.actorId },
      SampleTimings.requestTimeout
    );
    return { ...match, join };
  }
}

module.exports = { CreateMatchSessionPacketHandler };
