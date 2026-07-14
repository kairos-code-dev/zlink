import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_CLIENT, ZLINK_SPOT_HANDLE_RESOLVER, ZLINK_SPOT_OUTBOUND } from '@zlink-systems/nestjs';
import { courierActorNodeRid, SampleTimings } from '../../Shared/Configuration/sample-names';
import { actorRefFromMessage, bindCourier, ensureCourierActor, PacketNames } from '../../Shared/Contracts/messages';
import type {
  ActorRef,
  ZLinkActorClient,
  ZLinkMessage,
  ZLinkLocationStore,
  ZLinkSession,
  ZLinkSessionContext,
  ZLinkSessionDispatchContext,
  ZLinkSessionFactory,
  ZLinkSpotHandleResolver,
  ZLinkSpotOutbound
} from '@zlink-systems/framework';
import type {
  BindCourierRes,
  BindCourierSessionReq,
  EnsureCourierActorRes
} from '../../Shared/Contracts/messages';

class CourierSession implements ZLinkSession {
  constructor(
    readonly context: ZLinkSessionContext,
    private readonly spotHandles: ZLinkSpotHandleResolver,
    private readonly spotOutbound: ZLinkSpotOutbound,
    private readonly actors: ZLinkActorClient,
    private readonly locations: ZLinkLocationStore
  ) {}

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage): Promise<void> {
    if (dispatch.packetName === PacketNames.courierDecision) {
      const decision = payload.decode<{ courierId: string }>(Object as never);
      const actor = this.context.actors.find(decision.courierId);
      if (actor === undefined) {
        throw new Error(`Courier '${decision.courierId}' is not bound to this session.`);
      }
      await actor.relay(payload);
      return;
    }
    if (dispatch.packetName !== PacketNames.bindCourierSession) {
      throw new Error(`Unsupported courier session packet '${dispatch.packetName}'.`);
    }

    const request = payload.decode<BindCourierSessionReq>(Object as never);
    const sessionRoute = this.context.sessionId;
    const actorRef = await this.findOrEnsureActor(request.courierId);
    const actor = await this.context.actors.bindOrGet(actorRef);
    await this.actors.requestToActor(actorRef, bindCourier(request.courierId, sessionRoute))
      .timeout(SampleTimings.requestTimeout)
      .submit<BindCourierRes>();
    console.error(`deliverydispatch courier-session: bound courier=${request.courierId} actor=${actorRef.actorId}`);
    await actor.relay(payload);
  }

  private async findOrEnsureActor(courierId: string): Promise<ActorRef> {
    const found = await this.locations.resolveActor({ actorId: courierId });
    if (found !== undefined) {
      console.error(`deliverydispatch courier-session: found existing courier=${courierId}`);
      return found.actorRef;
    }
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
  constructor(
    @Inject(ZLINK_SPOT_HANDLE_RESOLVER) private readonly spotHandles: ZLinkSpotHandleResolver,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly spotOutbound: ZLinkSpotOutbound,
    @Inject(ZLINK_ACTOR_CLIENT) private readonly actors: ZLinkActorClient,
    @Inject('DELIVERYDISPATCH_LOCATION_STORE') private readonly locations: ZLinkLocationStore
  ) {}

  async create(context: ZLinkSessionContext): Promise<CourierSession> {
    return new CourierSession(context, this.spotHandles, this.spotOutbound, this.actors, this.locations);
  }
}

export {
  CourierSession,
  CourierSessionFactory
};
