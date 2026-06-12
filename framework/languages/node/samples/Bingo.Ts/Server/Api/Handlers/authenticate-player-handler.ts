const {
  authenticatePlayerAccepted,
  authenticatePlayerRejected
} = require('../../../Shared/Contracts/messages');
import type { ZLinkRequestHandler } from '../../../../../packages/framework/dist';
import type {
  AuthenticatePlayerRes,
  AuthenticateReq
} from '../../../Shared/Contracts/messages';

class AuthenticatePlayerHandler implements ZLinkRequestHandler<AuthenticateReq, AuthenticatePlayerRes> {
  async handle(request: AuthenticateReq): Promise<AuthenticatePlayerRes> {
    if (!request.accessToken?.startsWith('player-')) {
      return authenticatePlayerRejected('Access token must be a sample player id.');
    }

    return authenticatePlayerAccepted(request.accessToken);
  }
}

export { AuthenticatePlayerHandler };
