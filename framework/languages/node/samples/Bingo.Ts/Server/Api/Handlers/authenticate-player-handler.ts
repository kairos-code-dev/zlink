import { PacketNames, authenticatePlayerAccepted, authenticatePlayerRejected } from '../../../Shared/Contracts/messages';
import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type {
  AuthenticatePlayerRes,
  AuthenticatePlayerReq
} from '../../../Shared/Contracts/messages';

@zlinkRequestHandler('api', PacketNames.authenticatePlayerReq)
class AuthenticatePlayerHandler implements ZLinkRequestHandler<AuthenticatePlayerReq, AuthenticatePlayerRes> {
  async handle(request: AuthenticatePlayerReq): Promise<AuthenticatePlayerRes> {
    if (!request.accessToken?.startsWith('player-')) {
      return authenticatePlayerRejected('Access token must be a sample player id.');
    }

    return authenticatePlayerAccepted(request.accessToken);
  }
}

export { AuthenticatePlayerHandler };
