import { PacketNames } from '../../../Shared/Contracts/messages';
import { SupportChatStateStore } from '../../Support/supportchat-state';
import type { AuthenticateUserReq, AuthenticateUserRes } from '../../../Shared/Contracts/messages';

class AuthenticateUserHandler {
  constructor(private readonly store: SupportChatStateStore) {}

  async handle(request: AuthenticateUserReq): Promise<AuthenticateUserRes> {
    const auth = this.store.authenticate(request.accessToken);
    return {
      accepted: true,
      actorId: auth.actorId,
      displayName: auth.displayName,
      role: auth.role
    };
  }

  packetName(): string {
    return PacketNames.authenticateUserReq;
  }
}

export { AuthenticateUserHandler };
