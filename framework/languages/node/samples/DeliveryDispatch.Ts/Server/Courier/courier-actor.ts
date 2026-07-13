import { OfferDeliveryNotify } from '../../Shared/Contracts/messages';
import type { BindCourierReq, BindCourierRes, BindCourierSessionReq, BindCourierSessionRes, CourierDecisionMsg, DeliveryDispatchActorRef, OfferDeliveryReq, OfferDeliveryRes } from '../../Shared/Contracts/messages';
import type { ZLinkActor, ZLinkActorContext, ZLinkActorFactory } from '@zlink-systems/framework';

class CourierActor implements ZLinkActor {
  private readonly pending = new Map<string, (decision: CourierDecisionMsg) => void>();
  private actorRef: DeliveryDispatchActorRef | undefined;
  private sessionRoute: string | undefined;

  constructor(readonly actorId: string, readonly context: ZLinkActorContext) {}

  setActorRef(actorRef: DeliveryDispatchActorRef): void {
    this.actorRef = actorRef;
  }

  bindSession(request: BindCourierReq): BindCourierRes {
    this.sessionRoute = request.sessionRoute;
    return {
      courierId: request.courierId,
      actor: this.requireActorRef(),
      sessionRoute: request.sessionRoute
    };
  }

  bindRelayedSession(request: BindCourierSessionReq): BindCourierSessionRes {
    const actor = request.actor ?? this.requireActorRef();
    const sessionRoute = request.sessionRoute ?? this.sessionRoute;
    if (sessionRoute === undefined) throw new Error(`Courier actor '${this.actorId}' has no bound session route.`);
    this.setActorRef(actor);
    return {
      courierId: request.courierId,
      actor,
      sessionRoute
    };
  }

  async offer(request: OfferDeliveryReq): Promise<OfferDeliveryRes> {
    const decision = new Promise<CourierDecisionMsg>((resolve) => {
      this.pending.set(request.deliveryId, resolve);
    });
    this.context.boundSession.send(new OfferDeliveryNotify(
      request.courierId,
      request.deliveryId,
      request.pickupAddress,
      request.dropoffAddress
    )).submit();
    const resolved = await decision;
    return {
      deliveryId: resolved.deliveryId,
      courierId: resolved.courierId,
      accepted: resolved.accepted,
      reason: resolved.reason
    };
  }

  decide(decision: CourierDecisionMsg): void {
    const pending = this.pending.get(decision.deliveryId);
    if (pending === undefined) {
      return;
    }
    this.pending.delete(decision.deliveryId);
    pending(decision);
  }

  private requireActorRef(): DeliveryDispatchActorRef {
    if (this.actorRef === undefined) {
      throw new Error(`Courier actor '${this.actorId}' has not joined its entry spot.`);
    }
    return this.actorRef;
  }
}

class CourierActorDirectory {
  private readonly actors = new Map<string, CourierActor>();
  add(actor: CourierActor): void { this.actors.set(actor.actorId, actor); }
  require(actorId: string): CourierActor {
    const actor = this.actors.get(actorId);
    if (actor === undefined) throw new Error(`Courier actor '${actorId}' is not active.`);
    return actor;
  }
}

class CourierActorFactory implements ZLinkActorFactory {
  private static directory: CourierActorDirectory | undefined;
  static useDirectory(directory: CourierActorDirectory): void { this.directory = directory; }
  async create(actorId: string, context: ZLinkActorContext): Promise<CourierActor> {
    const actor = new CourierActor(actorId, context);
    if (CourierActorFactory.directory === undefined) throw new Error('CourierActorDirectory is not configured.');
    CourierActorFactory.directory.add(actor);
    return actor;
  }
}

export {
  CourierActor,
  CourierActorFactory,
  CourierActorDirectory
};
