import { SupportChatSession } from '../supportchat-session';
import type { AuthenticateReq, AuthenticateRes } from '../../../../Shared/Contracts/messages';

class AuthenticateSessionHandler {
  handle(session: SupportChatSession, request: AuthenticateReq): AuthenticateRes {
    return session.authenticate(request);
  }
}

export { AuthenticateSessionHandler };
