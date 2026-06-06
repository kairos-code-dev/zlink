const { Inject } = require('@nestjs/common');
const { PacketNames } = require('../../../../Shared/Contracts/messages');
const { SampleNames, SampleTimings } = require('../../../../Shared/Configuration/sample-names');
const { PLAY_ROUTE_CLIENT } = require('../../session-tokens');

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

Inject(PLAY_ROUTE_CLIENT)(PlaceMarkSessionPacketHandler, undefined, 0);

module.exports = { PlaceMarkSessionPacketHandler };
