import { Inject, Injectable } from '@nestjs/common';
import { ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import { SampleNames } from '../../../Configuration/sample-names';
import { retry } from '../../../runtime-support';
import {
  authenticateRes,
  authenticateUserReq,
  ensureSupportUserActorReq
} from '../../../../Shared/Contracts/messages';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import type {
  AuthenticateReq,
  AuthenticateRes,
  AuthenticateUserRes,
  EnsureSupportUserActorRes
} from '../../../../Shared/Contracts/messages';

type AuthenticateSessionContext = {
  setAuthenticated(actorId: string, displayName: string, role: string): void;
};

// Session lifecycle: AuthenticateReq is the only packet the Session server interprets itself.
// It validates the token on the Api server, ensures the Support actor exists, and binds the
// current stream session to that actor. On reconnect the same actor is reused.
@Injectable()
class SupportChatSessionAuthenticator {
  constructor(@Inject(ZLINK_CHANNEL_CLIENT) private readonly zlinkClient: ZLinkChannelClient) {}

  async handle(request: AuthenticateReq, context: AuthenticateSessionContext): Promise<AuthenticateRes> {
    const authenticated = await retry(() => this.zlinkClient
        .requestToChannel(SampleNames.apiChannel, authenticateUserReq(request.accessToken))
        .submit<AuthenticateUserRes>(), { delayMs: 25, maxAttempts: 200 });

    if (
      !authenticated.accepted ||
      authenticated.actorId === null ||
      authenticated.displayName === null ||
      authenticated.role === null
    ) {
      throw new Error(authenticated.reason ?? 'Support chat authentication failed.');
    }

    await retry(() => this.zlinkClient
        .requestToChannel(
          SampleNames.supportChannel,
          ensureSupportUserActorReq(authenticated.actorId, authenticated.displayName, authenticated.role)
        )
        .submit<EnsureSupportUserActorRes>(), { delayMs: 25, maxAttempts: 200 });

    context.setAuthenticated(authenticated.actorId, authenticated.displayName, authenticated.role);
    return authenticateRes(authenticated.actorId, authenticated.displayName, authenticated.role);
  }
}

export { SupportChatSessionAuthenticator };
