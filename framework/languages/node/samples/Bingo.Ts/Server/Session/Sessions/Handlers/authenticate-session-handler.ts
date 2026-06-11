const { Inject } = require('@nestjs/common');
const { ZLINK_CHANNEL_CLIENT } = require('../../../../../../../packages/nestjs/dist');
const { SampleNames, SampleTimings } = require('../../../Configuration/sample-names');
const {
  PacketNames,
  authenticateReq,
  authenticateSessionRes,
  ensurePlayerActorReq
} = require('../../../../Shared/Contracts/messages');
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

class AuthenticateSessionHandler {
  [key: string]: any;
  constructor(zlinkClient: any) {
    this.zlinkClient = zlinkClient;
  }

  async handle(request: AuthenticateReq, context: AuthenticateSessionContext): Promise<AuthenticateSessionRes> {
    const authenticated: AuthenticatePlayerRes = await this.zlinkClient
      .requestToChannel(SampleNames.apiChannel, authenticateReq(request.accessToken))
      .packetName(PacketNames.authenticatePlayerReq)
      .timeout(SampleTimings.requestTimeout)
      .submit();

    if (!authenticated.accepted || !authenticated.actorId || !authenticated.displayName) {
      throw new Error(authenticated.reason ?? 'Player authentication failed.');
    }

    const ensured: EnsurePlayerActorRes = await this.zlinkClient
      .requestToChannel(SampleNames.playChannel, ensurePlayerActorReq(authenticated.actorId, authenticated.displayName))
      .packetName(PacketNames.ensurePlayerActorReq)
      .timeout(SampleTimings.requestTimeout)
      .submit();

    await context.actors.bind(ensured.actor);
    context.actorId = ensured.actorId;
    context.displayName = authenticated.displayName;
    return authenticateSessionRes(ensured.actorId, authenticated.displayName);
  }
}

Inject(ZLINK_CHANNEL_CLIENT)(AuthenticateSessionHandler, undefined, 0);

export { AuthenticateSessionHandler };
