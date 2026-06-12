const {
  PacketNames,
  authenticatePlayerAccepted,
  authenticatePlayerRejected
} = require('../../../Shared/Contracts/messages');
const { zlinkRequestHandler } = require('../../../../../../packages/nestjs/dist');
import type { ZLinkRequestHandler } from '../../../../../packages/framework/dist';
import type {
  AuthenticatePlayerRes,
  AuthenticateReq
} from '../../../Shared/Contracts/messages';

@zlinkRequestHandler('api', PacketNames.authenticatePlayerReq)
class AuthenticatePlayerHandler implements ZLinkRequestHandler<AuthenticateReq, AuthenticatePlayerRes> {
  async handle(request: AuthenticateReq): Promise<AuthenticatePlayerRes> {
    if (!request.accessToken?.startsWith('player-')) {
      return authenticatePlayerRejected('Access token must be a sample player id.');
    }

    return authenticatePlayerAccepted(request.accessToken);
  }
}

export { AuthenticatePlayerHandler };
