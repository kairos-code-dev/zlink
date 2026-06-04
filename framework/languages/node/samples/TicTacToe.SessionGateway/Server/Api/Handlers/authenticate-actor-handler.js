const { actorDisplayName } = require('../../../Shared/Contracts/messages');

class AuthenticateActorHandler {
  handle(request) {
    if (typeof request.accessToken !== 'string' || request.accessToken.length === 0) {
      throw new Error('accessToken is required.');
    }
    return {
      actorId: request.accessToken,
      displayName: actorDisplayName(request.accessToken)
    };
  }
}

module.exports = { AuthenticateActorHandler };
