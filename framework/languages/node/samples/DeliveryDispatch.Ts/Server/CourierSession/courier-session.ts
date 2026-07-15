import { Inject, Injectable } from '@nestjs/common';
import { ZLINK_ACTOR_CLIENT, ZLINK_SPOT_HANDLE_RESOLVER, ZLINK_SPOT_OUTBOUND } from '@zlink-systems/nestjs';
import { courierActorNodeRid, SampleTimings } from '../../Shared/Configuration/sample-names';
import { actorRefFromMessage, bindCourier, ensureCourierActor, PacketNames } from '../../Shared/Contracts/messages';
import {
  ZLinkPacket,
  type ActorRef,
  type ZLinkActorClient,
  type ZLinkLocationStore,
  type ZLinkMessage,
  type ZLinkSession,
  type ZLinkSessionContext,
  type ZLinkSessionDispatchContext,
  type ZLinkSessionFactory,
  type ZLinkSpotHandleResolver,
  type ZLinkSpotOutbound
} from '@zlink-systems/framework';
import type { BindCourierRes, BindCourierSessionReq, EnsureCourierActorRes } from '../../Shared/Contracts/messages';

class CourierSession implements ZLinkSession {
  constructor(readonly context: ZLinkSessionContext) {
    context.handlers.addHandler(BindCourierSessionHandler);
  }

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage): Promise<void> {
    if (await this.context.handlers.tryHandle(dispatch, payload)) return;
    const actor = this.context.actors.bound.length === 1 ? this.context.actors.bound[0] : undefined;
    if (actor === undefined) throw new Error(`BindCourierSessionReq is required before '${dispatch.packetName}'.`);
    await actor.relay(payload);
  }
}

@Injectable()
@ZLinkPacket(PacketNames.bindCourierSession)
class BindCourierSessionHandler {
  constructor(
    @Inject(ZLINK_SPOT_HANDLE_RESOLVER) private readonly spotHandles: ZLinkSpotHandleResolver,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly spotOutbound: ZLinkSpotOutbound,
    @Inject(ZLINK_ACTOR_CLIENT) private readonly actors: ZLinkActorClient,
    @Inject('DELIVERYDISPATCH_LOCATION_STORE') private readonly locations: ZLinkLocationStore
  ) {}

  async handle(context: ZLinkSessionContext, _dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage): Promise<void> {
    const request = payload.decode<BindCourierSessionReq>(Object as never);
    const actorRef = await this.findOrEnsureActor(request.courierId);
    const actor = await context.actors.bindOrGet(actorRef);
    await this.actors.requestToActor(actorRef, bindCourier(request.courierId, context.sessionId))
      .timeout(SampleTimings.requestTimeout)
      .submit<BindCourierRes>();
    console.error(`deliverydispatch courier-session: bound courier=${request.courierId} actor=${actorRef.actorId}`);
    await actor.relay(payload);
  }

  private async findOrEnsureActor(courierId: string): Promise<ActorRef> {
    const found = await this.locations.resolveActor({ actorId: courierId });
    if (found !== undefined) return found.actorRef;
    const entrySpot = await this.spotHandles.resolveSpotHandle(courierActorNodeRid(courierId));
    if (entrySpot === undefined) throw new Error(`Courier entry spot was not found for '${courierId}'.`);
    const ensured = await this.spotOutbound
      .requestToSpot(entrySpot, ensureCourierActor(courierId))
      .timeout(SampleTimings.requestTimeout)
      .submit<EnsureCourierActorRes>();
    return actorRefFromMessage(ensured.actor);
  }
}

class CourierSessionFactory implements ZLinkSessionFactory<CourierSession> {
  async create(context: ZLinkSessionContext): Promise<CourierSession> {
    return new CourierSession(context);
  }
}

export { BindCourierSessionHandler, CourierSession, CourierSessionFactory };
