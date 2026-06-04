const { PacketNames } = require('../../../../Shared/Contracts/messages');
const { SampleNames, SampleTimings } = require('../../../../Shared/Configuration/sample-names');

class PlaceMarkSessionPacketHandler {
  constructor(playClient) {
    this.playClient = playClient;
  }

  async handle(request, session) {
    return await this.playClient.request(
      SampleNames.playNode,
      PacketNames.placeMarkReq,
      { matchId: request.matchId, actorId: session.actorId, cell: request.cell },
      SampleTimings.requestTimeout
    );
  }
}

module.exports = { PlaceMarkSessionPacketHandler };
