import { Inject, Injectable } from '@nestjs/common';
import { ZLINK_CHANNEL_CLIENT, ZLINK_ROUTE_CLIENT } from '@zlink-systems/nestjs';
import { BINGO_SAMPLE_CONFIG } from '../../../Configuration/sample-config';
import { SampleNames } from '../../../Configuration/sample-names';
import { retry } from '../../../runtime-support';
import { PacketNames, authenticatePlayerReq, authenticateSessionRes, ensurePlayerActorReq } from '../../../../Shared/Contracts/messages';
import type { RoutingId, ZLinkChannelClient, ZLinkRouteClient } from '@zlink-systems/framework';
import type { BingoSampleConfig } from '../../../Configuration/sample-config';
import type {
  AuthenticatePlayerRes,
  AuthenticateReq,
  AuthenticateSessionRes,
  EnsurePlayerActorRes
} from '../../../../Shared/Contracts/messages';

type AuthenticateSessionContext = {
  actors: {
    bind(actor: {
      nodeRid: RoutingId;
      actorId: string;
      generation: bigint;
    }): Promise<unknown>;
  };
  actorId: string | null;
  displayName: string | null;
};

@Injectable()
class SessionAuthenticator {
  constructor(
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly zlinkClient: ZLinkChannelClient,
    @Inject(ZLINK_ROUTE_CLIENT) private readonly routeClient: ZLinkRouteClient,
    @Inject(BINGO_SAMPLE_CONFIG) private readonly config: BingoSampleConfig
  ) {}

  async handle(request: AuthenticateReq, context: AuthenticateSessionContext): Promise<AuthenticateSessionRes> {
    console.log(`session-auth request api actor=${request.accessToken}`);
    const authenticated = await retry(() => this.zlinkClient
        .requestToChannel(SampleNames.apiChannel, authenticatePlayerReq(request.accessToken))
        .packetName(PacketNames.authenticatePlayerReq)
        .timeout(500)
        .submit<AuthenticatePlayerRes>(), { delayMs: 25, maxAttempts: 200 });
    console.log(`session-auth api accepted=${authenticated.accepted} actor=${authenticated.actorId ?? '-'}`);

    if (
      !authenticated.accepted ||
      authenticated.actorId === null ||
      authenticated.actorId.length === 0 ||
      authenticated.displayName === null ||
      authenticated.displayName.length === 0
    ) {
      throw new Error(authenticated.reason ?? 'Player authentication failed.');
    }

    console.log(`session-auth ensure actor=${authenticated.actorId}`);
    const ensureRequest = ensurePlayerActorReq(authenticated.actorId, authenticated.displayName);
    const ensured = await retry(async () => {
      if (this.config.preferredPlayNodeRid.length > 0) {
        return await this.routeClient
          .request(SampleNames.roomRouteChannel, this.config.preferredPlayNodeRid, ensureRequest)
          .packetName(PacketNames.ensurePlayerActorReq)
          .timeout(500)
          .submit<EnsurePlayerActorRes>(AbortSignal.timeout(500));
      }
      return await this.zlinkClient
        .requestToChannel(SampleNames.playChannel, ensureRequest)
        .packetName(PacketNames.ensurePlayerActorReq)
        .timeout(500)
        .submit<EnsurePlayerActorRes>();
    }, { delayMs: 25, maxAttempts: 200 });
    console.log(`session-auth ensured actor=${ensured.actorId} node=${ensured.actor.nodeRid}`);

    await context.actors.bind({
      ...ensured.actor,
      nodeRid: ensured.actor.nodeRid as unknown as RoutingId,
      generation: BigInt(ensured.actor.generation)
    });
    context.actorId = ensured.actorId;
    context.displayName = authenticated.displayName;
    return authenticateSessionRes(ensured.actorId, authenticated.displayName, ensured.actor.nodeRid);
  }
}

export { SessionAuthenticator };
