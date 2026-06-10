const { zlinkRequestHandler } = require('../../../../../../packages/nestjs/dist');
const { authenticatePlayerRes } = require('../../../Shared/Contracts/messages');
const { PacketNames } = require('../../../Shared/Contracts/messages');
import type {
  AuthenticatePlayerRes,
  AuthenticateReq
} from '../../../Shared/Contracts/messages';

class AuthenticatePlayerHandler {
  [key: string]: any;
  handle(request: AuthenticateReq): AuthenticatePlayerRes {
    if (typeof request.accessToken !== 'string' || request.accessToken.length === 0) {
      throw new Error('accessToken is required.');
    }
    return authenticatePlayerRes(request.accessToken);
  }
}

zlinkRequestHandler('api', PacketNames.authenticatePlayerReq)(AuthenticatePlayerHandler);

export { AuthenticatePlayerHandler };
