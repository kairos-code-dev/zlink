const {
  authenticatePlayerAccepted,
  authenticatePlayerRejected
} = require('../../../Shared/Contracts/messages');
import type {
  AuthenticatePlayerRes,
  AuthenticateReq
} from '../../../Shared/Contracts/messages';

class AuthenticatePlayerHandler {
  [key: string]: any;

  handle(request: AuthenticateReq): AuthenticatePlayerRes {
    if (!request.accessToken?.startsWith('player-')) {
      return authenticatePlayerRejected('Access token must be a sample player id.');
    }

    return authenticatePlayerAccepted(request.accessToken);
  }
}

export { AuthenticatePlayerHandler };
