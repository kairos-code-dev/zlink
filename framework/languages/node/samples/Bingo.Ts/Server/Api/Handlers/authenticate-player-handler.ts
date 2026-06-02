const { ZLinkHandlerGroup, ZLinkRequest } = require('../../../../../../packages/framework/dist');

class AuthenticatePlayerHandler {
  [key: string]: any;
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

ZLinkHandlerGroup('api')(AuthenticatePlayerHandler);
ZLinkRequest('AuthenticatePlayerReq')(AuthenticatePlayerHandler.prototype, 'handle', descriptor());

export { AuthenticatePlayerHandler };

function descriptor() {
  return {
    configurable: true,
    enumerable: false,
    value: AuthenticatePlayerHandler.prototype.handle,
    writable: true
  };
}
