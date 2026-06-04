const { PacketNames } = require('../../../../Shared/Contracts/messages');
const { SampleNames, SampleTimings } = require('../../../../Shared/Configuration/sample-names');
const { SessionRelaySession } = require('../session-relay-session');

class AuthenticateSessionPacketHandler {
  constructor(apiClient, playClient) {
    this.apiClient = apiClient;
    this.playClient = playClient;
  }

  async handle(request) {
    const authenticated = await this.apiClient.request(
      SampleNames.apiNode,
      PacketNames.authenticateActorReq,
      { accessToken: request.accessToken },
      SampleTimings.requestTimeout
    );
    const session = new SessionRelaySession(authenticated.actorId, authenticated.displayName, request.sessionId);
    await this.playClient.request(
      SampleNames.playNode,
      PacketNames.ensurePlayerActorReq,
      { actorId: session.actorId, sessionId: session.sessionId },
      SampleTimings.requestTimeout
    );
    return { session, response: authenticated };
  }
}

module.exports = { AuthenticateSessionPacketHandler };
