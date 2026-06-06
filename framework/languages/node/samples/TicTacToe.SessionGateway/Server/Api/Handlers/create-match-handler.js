const { Inject } = require('@nestjs/common');
const { PacketNames } = require('../../../Shared/Contracts/messages');
const { SampleNames, SampleTimings } = require('../../../Shared/Configuration/sample-names');
const { PLAY_ROUTE_CLIENT } = require('../api-tokens');

class CreateMatchHandler {
  constructor(playClient) {
    this.playClient = playClient;
  }

  async handle(request) {
    return await this.playClient.request(
      SampleNames.playNode,
      PacketNames.createMatchReq,
      { matchName: request.matchName ?? 'match' },
      SampleTimings.requestTimeout
    );
  }
}

Inject(PLAY_ROUTE_CLIENT)(CreateMatchHandler, undefined, 0);

module.exports = { CreateMatchHandler };
