import { Inject, Injectable } from '@nestjs/common';
import {
  ZLINK_ALLOCATED_ROUTING_ID_PROVIDER,
  ZLINK_CHANNEL_CLIENT,
  ZLINK_SPOT_HANDLE_RESOLVER,
  ZLINK_ROUTE_CLIENT
} from '@zlink-systems/nestjs';
import { SampleNames } from '../../../Configuration/sample-names';
import {
  AuthenticatePlayerReq,
  AuthenticateRes,
  EnsurePlayerActorReq
} from '../../../../Shared/Contracts/bingo-messages.generated';
import { PacketNames } from '../../../../Shared/Contracts/messages';
import {
  ZLinkPacket,
  type RoutingId,
  type ZLinkAllocatedRoutingIdProvider,
  type ZLinkChannelClient,
  type ZLinkRouteClient,
  type ZLinkSpotHandleResolver,
  type ZLinkMessage,
  type ZLinkSessionContext,
  type ZLinkSessionDispatchContext
} from '@zlink-systems/framework';
import type {
  AuthenticatePlayerRes,
  AuthenticateReq,
  EnsurePlayerActorRes
} from '../../../../Shared/Contracts/messages';

@Injectable()
@ZLinkPacket(PacketNames.authenticateReq)
class SessionAuthenticator {
  constructor(
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly zlinkClient: ZLinkChannelClient,
    @Inject(ZLINK_ROUTE_CLIENT) private readonly routeClient: ZLinkRouteClient,
    @Inject(ZLINK_SPOT_HANDLE_RESOLVER) private readonly spotHandles: ZLinkSpotHandleResolver,
    @Inject(ZLINK_ALLOCATED_ROUTING_ID_PROVIDER)
    private readonly allocatedRoutingIds: ZLinkAllocatedRoutingIdProvider
  ) {}

  async handle(
    context: ZLinkSessionContext,
    _dispatch: ZLinkSessionDispatchContext,
    payload: ZLinkMessage
  ): Promise<void> {
    const request = payload.decode<AuthenticateReq>(Object as never);
    console.log(`session-auth request api actor=${request.accessToken}`);
    const authenticated = await this.zlinkClient
        .requestToChannel(
          SampleNames.roomSpotNode,
          SampleNames.apiChannel,
          new AuthenticatePlayerReq({ accessToken: request.accessToken })
        )
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
    const allocation = await this.allocatedRoutingIds.waitForReadyAllocation('bingo.session');
    const preferredPlayNodeRid = `play${allocation.slot}` as RoutingId;
    const ensureRequest = new EnsurePlayerActorReq({
      actorId: authenticated.actorId,
      displayName: authenticated.displayName,
      preferredActorNodeRid: preferredPlayNodeRid
    });
    const playEntrySpot = await this.spotHandles.resolveSpotHandle(
      SampleNames.roomSpotNode,
      preferredPlayNodeRid
    );
    if (playEntrySpot === undefined) {
      throw new Error(`Play entry spot '${preferredPlayNodeRid}' was not found.`);
    }
    const ensured = await this.routeClient
      .requestToSpot(playEntrySpot, ensureRequest)
      .timeout(500)
      .submit<EnsurePlayerActorRes>(AbortSignal.timeout(500));
    console.log(`session-auth ensured actor=${ensured.actorId} node=${ensured.actor.nodeRid}`);

    await context.actors.bindOrGet({
      ...ensured.actor,
      nodeRid: ensured.actor.nodeRid as unknown as RoutingId,
      generation: BigInt(ensured.actor.generation)
    });
    context.client.reply(new AuthenticateRes({
      actorId: ensured.actorId,
      displayName: authenticated.displayName,
      actorNodeRid: ensured.actor.nodeRid
    })).submit();
  }
}

export { SessionAuthenticator };
