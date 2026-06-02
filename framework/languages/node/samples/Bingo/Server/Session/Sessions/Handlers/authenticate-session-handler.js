const { SampleNames, SampleTimings } = require('../../../../Shared/Configuration/sample-names');

class AuthenticateSessionHandler {
  constructor(apiClient, playClient) {
    this.apiClient = apiClient;
    this.playClient = playClient;
  }

  async handle(request, context) {
    const authenticated = await this.apiClient
      .requestToChannel(SampleNames.apiChannel, { accessToken: request.accessToken })
      .packetName('AuthenticatePlayerReq')
      .timeout(SampleTimings.requestTimeout)
      .submit();

    if (!authenticated.accepted || !authenticated.actorId || !authenticated.displayName) {
      throw new Error(authenticated.reason ?? 'Player authentication failed.');
    }

    const ensured = await this.playClient
      .requestToChannel(SampleNames.playChannel, {
        actorId: authenticated.actorId,
        displayName: authenticated.displayName
      })
      .packetName('EnsurePlayerActorReq')
      .timeout(SampleTimings.requestTimeout)
      .submit();

    await context.actors.bind(ensured.actor);
    context.actorId = ensured.actorId;
    context.displayName = authenticated.displayName;
    return {
      actorId: ensured.actorId,
      displayName: authenticated.displayName
    };
  }
}

module.exports = { AuthenticateSessionHandler };
