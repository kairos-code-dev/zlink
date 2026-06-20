import { PacketNames, authenticatePlayerAccepted, authenticatePlayerRejected } from '../../../Shared/Contracts/messages';
import { SampleNames } from '../../Configuration/sample-names';
import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type {
  AuthenticatePlayerRes,
  AuthenticatePlayerReq
} from '../../../Shared/Contracts/messages';

@zlinkRequestHandler('api', PacketNames.authenticatePlayerReq)
class AuthenticatePlayerHandler implements ZLinkRequestHandler<AuthenticatePlayerReq, AuthenticatePlayerRes> {
  async handle(request: AuthenticatePlayerReq): Promise<AuthenticatePlayerRes> {
    if (!(SampleNames.actorIds as readonly string[]).includes(request.accessToken)) {
      return authenticatePlayerRejected('Access token must be a sample player id.');
    }

    return authenticatePlayerAccepted(request.accessToken);
  }
}

export { AuthenticatePlayerHandler };
