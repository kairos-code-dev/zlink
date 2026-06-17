import { Inject, Injectable } from '@nestjs/common';
import { ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import { SampleNames } from '../../../Configuration/sample-names';
import { retry } from '../../../runtime-support';
import { authenticatePlayerReq, authenticateSessionRes, ensurePlayerActorReq } from '../../../../Shared/Contracts/messages';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import type {
  AuthenticatePlayerRes,
  AuthenticateReq,
  AuthenticateSessionRes,
  EnsurePlayerActorRes
} from '../../../../Shared/Contracts/messages';

type AuthenticateSessionContext = {
  actors: {
    bind(actor: EnsurePlayerActorRes['actor']): Promise<void>;
  };
  actorId: string | null;
  displayName: string | null;
};

@Injectable()
class SessionAuthenticator {
  constructor(@Inject(ZLINK_CHANNEL_CLIENT) private readonly zlinkClient: ZLinkChannelClient) {}

  async handle(request: AuthenticateReq, context: AuthenticateSessionContext): Promise<AuthenticateSessionRes> {
    const authenticated = await retry(() => this.zlinkClient
        .requestToChannel(SampleNames.apiChannel, authenticatePlayerReq(request.accessToken))
        .submit<AuthenticatePlayerRes>(), { delayMs: 25, maxAttempts: 200 });

    if (
      !authenticated.accepted ||
      authenticated.actorId === null ||
      authenticated.actorId.length === 0 ||
      authenticated.displayName === null ||
      authenticated.displayName.length === 0
    ) {
      throw new Error(authenticated.reason ?? 'Player authentication failed.');
    }

    const ensured = await retry(() => this.zlinkClient
        .requestToChannel(
          SampleNames.playChannel,
          ensurePlayerActorReq(authenticated.actorId, authenticated.displayName)
        )
        .submit<EnsurePlayerActorRes>(), { delayMs: 25, maxAttempts: 200 });

    await context.actors.bind(ensured.actor);
    context.actorId = ensured.actorId;
    context.displayName = authenticated.displayName;
    return authenticateSessionRes(ensured.actorId, authenticated.displayName);
  }
}

export { SessionAuthenticator };
