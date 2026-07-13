import type { AuthenticateReq, AuthenticateRes } from '../../../Shared/Contracts/messages';
import { PacketNames } from '../../../Shared/Contracts/messages';
import type {
  ZLinkMessage,
  ZLinkSession,
  ZLinkSessionContext,
  ZLinkSessionDispatchContext,
  ZLinkSessionFactory
} from '@zlink-systems/framework';

class SupportChatSession implements ZLinkSession {
  actorId?: string;

  constructor(readonly context: ZLinkSessionContext) {}

  authenticate(request: AuthenticateReq): AuthenticateRes {
    const role = request.accessToken.startsWith('agent-') ? 'Agent' : 'Customer';
    this.actorId = request.accessToken;
    return {
      actorId: request.accessToken,
      displayName: `${role} ${request.accessToken}`,
      role
    };
  }

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage): Promise<void> {
    if (dispatch.packetName !== PacketNames.authenticateReq) return;
    this.context.client.reply(this.authenticate(payload.decode<AuthenticateReq>())).submit();
  }
}

class SupportChatSessionFactory implements ZLinkSessionFactory<SupportChatSession> {
  async create(context: ZLinkSessionContext): Promise<SupportChatSession> {
    return new SupportChatSession(context);
  }
}

export { SupportChatSession, SupportChatSessionFactory };
