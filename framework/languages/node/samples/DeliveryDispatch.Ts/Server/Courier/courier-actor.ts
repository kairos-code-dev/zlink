import { Inject, Injectable } from '@nestjs/common';
import { ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import { OfferDeliveryNotify, OfferDeliveryResultMsg } from '../../Shared/Contracts/messages';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import type { BindCourierReq, BindCourierRes, BindCourierSessionReq, BindCourierSessionRes, CourierDecisionMsg, DeliveryDispatchActorRef, OfferDeliveryMsg } from '../../Shared/Contracts/messages';
import type { ZLinkActor, ZLinkActorContext, ZLinkActorFactory, ZLinkChannelClient } from '@zlink-systems/framework';

class CourierActor implements ZLinkActor {
  private actorRef: DeliveryDispatchActorRef | undefined;
  private sessionRoute: string | undefined;

  constructor(
    readonly actorId: string,
    readonly context: ZLinkActorContext,
    private readonly channels: ZLinkChannelClient
  ) {}

  setActorRef(actorRef: DeliveryDispatchActorRef): void {
    this.actorRef = actorRef;
  }

  actorReference(): DeliveryDispatchActorRef {
    return this.requireActorRef();
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

  async offer(request: OfferDeliveryMsg): Promise<void> {
    await this.context.boundSession.send(new OfferDeliveryNotify(
      request.courierId,
      request.deliveryId,
      request.attempt,
      request.pickupAddress,
      request.dropoffAddress
    )).submit();
  }

  async decide(decision: CourierDecisionMsg): Promise<void> {
    await this.channels.sendToChannel(
      SampleNames.dispatchChannel,
      new OfferDeliveryResultMsg(
        decision.deliveryId,
        decision.courierId,
        decision.attempt,
        decision.accepted,
        decision.reason
      )
    ).submit();
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

@Injectable()
class CourierActorFactory implements ZLinkActorFactory {
  constructor(
    private readonly directory: CourierActorDirectory,
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient
  ) {}

  async create(actorId: string, context: ZLinkActorContext): Promise<CourierActor> {
    const actor = new CourierActor(actorId, context, this.channels);
    this.directory.add(actor);
    return actor;
  }
}

export {
  CourierActor,
  CourierActorFactory,
  CourierActorDirectory
};
