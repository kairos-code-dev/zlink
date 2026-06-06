class AuthenticatePlayerHandler {
  handle(request) {
    if (!request.accessToken?.startsWith('player-')) {
      return {
        accepted: false,
        actorId: null,
        displayName: null,
        reason: 'Access token must be a sample player id.'
      };
    }

    return {
      accepted: true,
      actorId: request.accessToken,
      displayName: request.accessToken.replace('player-', 'Player '),
      reason: null
    };
  }
}

module.exports = { AuthenticatePlayerHandler };
