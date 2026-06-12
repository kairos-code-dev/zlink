const { Inject, Injectable } = require('@nestjs/common');
const { ZLINK_CHANNEL_CLIENT } = require('../../../../../../../packages/nestjs/dist');
const { SampleNames, SampleTimings } = require('../../../Configuration/sample-names');
const {
  PacketNames,
  authenticateReq,
  authenticateSessionRes,
  ensurePlayerActorReq
} = require('../../../../Shared/Contracts/messages');
import type { ZLinkChannelClient } from '../../../../../../packages/framework/dist';
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
    const authenticated = await this.zlinkClient
      .requestToChannel(SampleNames.apiChannel, authenticateReq(request.accessToken))
      .packetName(PacketNames.authenticatePlayerReq)
      .timeout(SampleTimings.requestTimeout)
      .submit<AuthenticatePlayerRes>();

    if (!authenticated.accepted || !authenticated.actorId || !authenticated.displayName) {
      throw new Error(authenticated.reason ?? 'Player authentication failed.');
    }

    const ensured = await this.zlinkClient
      .requestToChannel(SampleNames.playChannel, ensurePlayerActorReq(authenticated.actorId, authenticated.displayName))
      .packetName(PacketNames.ensurePlayerActorReq)
      .timeout(SampleTimings.requestTimeout)
      .submit<EnsurePlayerActorRes>();

    await context.actors.bind(ensured.actor);
    context.actorId = ensured.actorId;
    context.displayName = authenticated.displayName;
    return authenticateSessionRes(ensured.actorId, authenticated.displayName);
  }
}

export { SessionAuthenticator };
