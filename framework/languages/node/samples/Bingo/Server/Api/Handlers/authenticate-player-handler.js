class AuthenticatePlayerHandler {
  handle(request) {
    if (!request.accessToken?.startsWith('player-')) {
      return {
        authenticated: false,
        actorId: null,
        displayName: null,
        error: 'Access token must be a sample player id.'
      };
    }

    return {
      authenticated: true,
      actorId: request.accessToken,
      displayName: request.accessToken.replace('player-', 'Player '),
      error: null
    };
  }
}

module.exports = { AuthenticatePlayerHandler };
