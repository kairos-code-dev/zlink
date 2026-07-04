import { PacketNames } from '../../../Shared/Contracts/messages';
import type { AuthenticateReq, AuthenticateRes } from '../../../Shared/Contracts/messages';

class SupportChatSession {
  actorId?: string;

  authenticate(request: AuthenticateReq): AuthenticateRes {
    const role = request.accessToken.startsWith('agent-') ? 'Agent' : 'Customer';
    this.actorId = request.accessToken;
    return {
      actorId: request.accessToken,
      displayName: `${role} ${request.accessToken}`,
      role
    };
  }
}

class SupportChatSessionFactory {
  create(): SupportChatSession {
    return new SupportChatSession();
  }

  packetName(): string {
    return PacketNames.authenticateReq;
  }
}

export { SupportChatSession, SupportChatSessionFactory };
