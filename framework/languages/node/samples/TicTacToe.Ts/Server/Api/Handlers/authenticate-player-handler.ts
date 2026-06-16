import { authenticatePlayerRes } from '../../../Shared/Contracts/messages';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type {
  AuthenticatePlayerRes,
  AuthenticateReq
} from '../../../Shared/Contracts/messages';

class AuthenticatePlayerHandler implements ZLinkRequestHandler<AuthenticateReq, AuthenticatePlayerRes> {
  async handle(request: AuthenticateReq): Promise<AuthenticatePlayerRes> {
    if (typeof request.accessToken !== 'string' || request.accessToken.length === 0) {
      throw new Error('accessToken is required.');
    }
    return authenticatePlayerRes(request.accessToken);
  }
}

export { AuthenticatePlayerHandler };
