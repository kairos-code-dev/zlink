import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, zlinkEntrySpotActorRequestHandler, zlinkEntrySpotActorSendHandler, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { courierActorNodeRid, SampleNames } from '../../Shared/Configuration/sample-names';
import { EnsureCourierActorReq, OfferDeliveryReq, PacketNames, actorRefForMessage } from '../../Shared/Contracts/messages';
import type { ZLinkActorManager, ZLinkRequestContext, ZLinkRequestHandler } from '@zlink-systems/framework';
import type { BindCourierReq, BindCourierRes, BindCourierSessionReq, BindCourierSessionRes, CourierDecisionMsg, EnsureCourierActorRes, OfferDeliveryRes } from '../../Shared/Contracts/messages';
import { CourierActor, CourierActorDirectory } from './courier-actor';
import { CourierEntrySpot } from './courier-entry-spot';

@zlinkRequestHandler('courier-actor-node', PacketNames.offerDelivery)
class OfferDeliveryActorNodeHandler implements ZLinkRequestHandler<OfferDeliveryReq, OfferDeliveryRes> {
  constructor(private readonly directory: CourierActorDirectory) {}

  async handle(request: OfferDeliveryReq, context: ZLinkRequestContext): Promise<OfferDeliveryRes> {
    void context;
    const actor = this.directory.require(request.courierId);
    return await actor.offer(request);
  }
}

@zlinkRequestHandler('courier-actor-node', PacketNames.ensureCourierActor)
class EnsureCourierActorHandler implements ZLinkRequestHandler<EnsureCourierActorReq, EnsureCourierActorRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actors: ZLinkActorManager,
    private readonly directory: CourierActorDirectory
  ) {}
  async handle(request: EnsureCourierActorReq): Promise<EnsureCourierActorRes> {
    await this.actors.getOrCreate(request.courierId, SampleNames.courierActorType, request);
    const active = this.directory.require(request.courierId);
    const joined = await active.context.joinEntrySpot(courierActorNodeRid(request.courierId), request).submit();
    if (joined.status !== 'accepted') {
      throw new Error(`Courier '${request.courierId}' could not join its entry spot.`);
    }
    const actorRef = actorRefForMessage(joined.actor);
    active.setActorRef(actorRef);
    return { courierId: request.courierId, actor: actorRef };
  }
}

@zlinkEntrySpotActorRequestHandler({
  entrySpot: () => CourierEntrySpot,
  actor: () => CourierActor,
  packetName: PacketNames.offerDelivery
})
class CourierActorOfferHandler {
  async handle(_spot: CourierEntrySpot, actor: CourierActor, _context: unknown, request: OfferDeliveryReq): Promise<OfferDeliveryRes> {
    return await actor.offer(request);
  }
}

@zlinkEntrySpotActorRequestHandler({
  entrySpot: () => CourierEntrySpot,
  actor: () => CourierActor,
  packetName: PacketNames.bindCourier
})
class CourierActorBindHandler {
  handle(_spot: CourierEntrySpot, actor: CourierActor, _context: unknown, request: BindCourierReq): BindCourierRes {
    return actor.bindSession(request);
  }
}

@zlinkEntrySpotActorRequestHandler({
  entrySpot: () => CourierEntrySpot,
  actor: () => CourierActor,
  packetName: PacketNames.bindCourierSession
})
class CourierActorSessionBindHandler {
  handle(
    _spot: CourierEntrySpot,
    actor: CourierActor,
    _context: unknown,
    request: BindCourierSessionReq
  ): BindCourierSessionRes {
    return actor.bindRelayedSession(request);
  }
}

@zlinkEntrySpotActorSendHandler({
  entrySpot: () => CourierEntrySpot,
  actor: () => CourierActor,
  packetName: PacketNames.courierDecision
})
class CourierActorDecisionHandler {
  async handle(_spot: CourierEntrySpot, actor: CourierActor, _context: unknown, decision: CourierDecisionMsg): Promise<void> {
    actor.decide(decision);
  }
}

export {
  OfferDeliveryActorNodeHandler,
  EnsureCourierActorHandler,
  CourierActorOfferHandler,
  CourierActorBindHandler,
  CourierActorSessionBindHandler,
  CourierActorDecisionHandler
};
