import { Inject, Injectable } from '@nestjs/common';
import {
  ZLINK_CHANNEL_CLIENT,
  ZLINK_LOCATION_RUNTIME_QUERY,
  ZLINK_ROUTE_CLIENT
} from '@zlink-systems/nestjs';
import { BINGO_SAMPLE_CONFIG } from '../../../Configuration/sample-config';
import { SampleNames } from '../../../Configuration/sample-names';
import {
  AuthenticatePlayerReq,
  AuthenticateRes,
  EnsurePlayerActorReq
} from '../../../../Shared/Contracts/bingo-messages.generated';
import {
  ZLinkLocationAutoConnectType,
  ZLinkLocationRole,
  type RoutingId,
  type ZLinkChannelClient,
  type ZLinkLocationRuntimeQuery,
  type ZLinkRouteClient
} from '@zlink-systems/framework';
import type { BingoSampleConfig } from '../../../Configuration/sample-config';
import type {
  AuthenticatePlayerRes,
  AuthenticateReq,
  EnsurePlayerActorRes
} from '../../../../Shared/Contracts/messages';

type AuthenticateSessionContext = {
  actors: {
    bindOrGet(actor: {
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
    @Inject(ZLINK_LOCATION_RUNTIME_QUERY) private readonly locations: ZLinkLocationRuntimeQuery,
    @Inject(BINGO_SAMPLE_CONFIG) private readonly config: BingoSampleConfig
  ) {}

  async handle(request: AuthenticateReq, context: AuthenticateSessionContext): Promise<AuthenticateRes> {
    console.log(`session-auth request api actor=${request.accessToken}`);
    const authenticated = await this.zlinkClient
        .requestToChannel(SampleNames.apiChannel, new AuthenticatePlayerReq({ accessToken: request.accessToken }))
        .timeout(500)
        .submit<AuthenticatePlayerRes>();
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
    const ensureRequest = new EnsurePlayerActorReq({
      actorId: authenticated.actorId,
      displayName: authenticated.displayName,
      preferredActorNodeRid: this.config.preferredPlayNodeRid
    });
    const servingPeers = await this.locations.listPeerLocations({
      autoConnectType: ZLinkLocationAutoConnectType.RouteMesh,
      meshName: SampleNames.playChannel,
      role: ZLinkLocationRole.Router
    });
    const serving = servingPeers.filter((peer) => !peer.draining && peer.nodeRid !== undefined);
    const selected = serving.find((peer) => String(peer.nodeRid) === this.config.preferredPlayNodeRid)
      ?? serving.at(0);
    if (selected?.nodeRid === undefined) {
      throw new Error('No serving Play node is available for a new player actor.');
    }
    const ensured = await this.routeClient
      .requestToNode(SampleNames.playChannel, selected.nodeRid, ensureRequest)
      .timeout(500)
      .submit<EnsurePlayerActorRes>(AbortSignal.timeout(500));
    console.log(`session-auth ensured actor=${ensured.actorId} node=${ensured.actor.nodeRid}`);

    await context.actors.bindOrGet({
      ...ensured.actor,
      nodeRid: ensured.actor.nodeRid as unknown as RoutingId,
      generation: BigInt(ensured.actor.generation)
    });
    context.actorId = ensured.actorId;
    context.displayName = authenticated.displayName;
    return new AuthenticateRes({
      actorId: ensured.actorId,
      displayName: authenticated.displayName,
      actorNodeRid: ensured.actor.nodeRid
    });
  }
}

export { SessionAuthenticator };
