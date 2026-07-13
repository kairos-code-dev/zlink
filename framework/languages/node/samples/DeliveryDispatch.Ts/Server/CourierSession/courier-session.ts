import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_CLIENT, ZLINK_ROUTE_CLIENT } from '@zlink-systems/nestjs';
import { courierActorNodeRid, SampleNames, SampleTimings } from '../../Shared/Configuration/sample-names';
import { actorRefFromMessage, bindCourier, ensureCourierActor, PacketNames } from '../../Shared/Contracts/messages';
import type {
  ActorRef,
  ZLinkActorClient,
  ZLinkMessage,
  ZLinkLocationStore,
  ZLinkRouteClient,
  ZLinkSession,
  ZLinkSessionContext,
  ZLinkSessionDispatchContext,
  ZLinkSessionFactory
} from '@zlink-systems/framework';
import type {
  BindCourierRes,
  BindCourierSessionReq,
  EnsureCourierActorRes
} from '../../Shared/Contracts/messages';

class CourierSession implements ZLinkSession {
  constructor(
    readonly context: ZLinkSessionContext,
    private readonly routes: ZLinkRouteClient,
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
    const ensured = await this.routes
      .requestToNode(
        SampleNames.courierActorNodeRouteChannel,
        courierActorNodeRid(courierId),
        ensureCourierActor(courierId)
      )
      .timeout(SampleTimings.requestTimeout)
      .submit<EnsureCourierActorRes>();
    return actorRefFromMessage(ensured.actor);
  }
}

class CourierSessionFactory implements ZLinkSessionFactory<CourierSession> {
  constructor(
    @Inject(ZLINK_ROUTE_CLIENT) private readonly routes: ZLinkRouteClient,
    @Inject(ZLINK_ACTOR_CLIENT) private readonly actors: ZLinkActorClient,
    @Inject('DELIVERYDISPATCH_LOCATION_STORE') private readonly locations: ZLinkLocationStore
  ) {}

  async create(context: ZLinkSessionContext): Promise<CourierSession> {
    return new CourierSession(context, this.routes, this.actors, this.locations);
  }
}

export {
  CourierSession,
  CourierSessionFactory
};
