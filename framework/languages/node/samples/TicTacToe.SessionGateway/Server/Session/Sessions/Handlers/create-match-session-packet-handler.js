const { Inject } = require('@nestjs/common');
const { PacketNames } = require('../../../../Shared/Contracts/messages');
const { SampleNames, SampleTimings } = require('../../../../Shared/Configuration/sample-names');
const { API_ROUTE_CLIENT, PLAY_ROUTE_CLIENT } = require('../../session-tokens');

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

Inject(API_ROUTE_CLIENT)(CreateMatchSessionPacketHandler, undefined, 0);
Inject(PLAY_ROUTE_CLIENT)(CreateMatchSessionPacketHandler, undefined, 1);

module.exports = { CreateMatchSessionPacketHandler };
