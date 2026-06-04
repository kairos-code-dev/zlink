const { PacketNames } = require('../../../Shared/Contracts/messages');
const { SampleNames, SampleTimings } = require('../../../Shared/Configuration/sample-names');

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

module.exports = { CreateMatchHandler };
