import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import {
  PacketNames,
  SupportChatRoles,
  authenticateUserAccepted,
  authenticateUserRejected
} from '../../../Shared/Contracts/messages';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type {
  AuthenticateUserReq,
  AuthenticateUserRes
} from '../../../Shared/Contracts/messages';

// AuthenticationPort (inbound API boundary): validates a stream access token and returns the
// support actor identity (actor id, display name, role).
@zlinkRequestHandler('api', PacketNames.authenticateUserReq)
class AuthenticateUserHandler implements ZLinkRequestHandler<AuthenticateUserReq, AuthenticateUserRes> {
  async handle(request: AuthenticateUserReq): Promise<AuthenticateUserRes> {
    switch (request.accessToken) {
      case 'customer-1':
        return authenticateUserAccepted('customer-1', 'Customer 1', SupportChatRoles.customer);
      case 'customer-2':
        return authenticateUserAccepted('customer-2', 'Customer 2', SupportChatRoles.customer);
      case 'customer-3':
        return authenticateUserAccepted('customer-3', 'Customer 3', SupportChatRoles.customer);
      case 'agent-1':
        return authenticateUserAccepted('agent-1', 'Agent 1', SupportChatRoles.agent);
      case 'agent-2':
        return authenticateUserAccepted('agent-2', 'Agent 2', SupportChatRoles.agent);
      default:
        return authenticateUserRejected('Unknown support chat token.');
    }
  }
}

export { AuthenticateUserHandler };
