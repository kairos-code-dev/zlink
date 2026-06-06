const { Inject } = require('@nestjs/common');
const { ZLINK_CHANNEL_CLIENT } = require('../../../../../../packages/nestjs/dist');
const { SampleNames, SampleTimings } = require('../../../../Shared/Configuration/sample-names');

class AuthenticateSessionHandler {
  constructor(zlinkClient) {
    this.zlinkClient = zlinkClient;
  }

  async handle(request, context) {
    const authenticated = await this.zlinkClient
      .requestToChannel(SampleNames.apiChannel, { accessToken: request.accessToken })
      .packetName('AuthenticatePlayerReq')
      .timeout(SampleTimings.requestTimeout)
      .submit();

    if (!authenticated.accepted || !authenticated.actorId || !authenticated.displayName) {
      throw new Error(authenticated.reason ?? 'Player authentication failed.');
    }

    const ensured = await this.zlinkClient
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

Inject(ZLINK_CHANNEL_CLIENT)(AuthenticateSessionHandler, undefined, 0);

module.exports = { AuthenticateSessionHandler };
