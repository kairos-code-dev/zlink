import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_CLIENT, ZLINK_ACTOR_MANAGER, zlinkEntrySpotActorRequestHandler, zlinkEntrySpotActorSendHandler, zlinkEntrySpotPacketHandler } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import {
  EnsureCourierActorReq,
  OfferDeliveryMsg,
  PacketNames,
  actorRefForMessage,
  actorRefFromMessage
} from '../../Shared/Contracts/messages';
import type { ZLinkActorClient, ZLinkActorManager, ZLinkHandlerContext, ZLinkSpotPacketHandler, ZLinkSpotRequestHandler } from '@zlink-systems/framework';
import type { BindCourierReq, BindCourierRes, BindCourierSessionReq, BindCourierSessionRes, CourierDecisionMsg, EnsureCourierActorRes } from '../../Shared/Contracts/messages';
import { CourierActor, CourierActorDirectory } from './courier-actor';
import { CourierEntrySpot } from './courier-entry-spot';

@zlinkEntrySpotPacketHandler({ entrySpot: () => CourierEntrySpot, packetName: PacketNames.offerDelivery })
class OfferDeliveryEntrySpotHandler implements ZLinkSpotPacketHandler<CourierEntrySpot, OfferDeliveryMsg> {
  constructor(
    private readonly directory: CourierActorDirectory,
    @Inject(ZLINK_ACTOR_CLIENT) private readonly actors: ZLinkActorClient
  ) {}

  async handle(_spot: CourierEntrySpot, request: OfferDeliveryMsg, context: ZLinkHandlerContext): Promise<void> {
    void context;
    const actor = this.directory.require(request.courierId);
    this.actors.sendToActor(
      actorRefFromMessage(actor.actorReference()),
      new OfferDeliveryMsg(
        request.courierId,
        request.deliveryId,
        request.attempt,
        request.pickupAddress,
        request.dropoffAddress
      )
    ).submit();
  }
}

@zlinkEntrySpotPacketHandler({ entrySpot: () => CourierEntrySpot, packetName: PacketNames.ensureCourierActor })
class EnsureCourierActorHandler implements ZLinkSpotRequestHandler<CourierEntrySpot, EnsureCourierActorReq, EnsureCourierActorRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actors: ZLinkActorManager,
    private readonly directory: CourierActorDirectory
  ) {}
  async handle(_spot: CourierEntrySpot, request: EnsureCourierActorReq): Promise<EnsureCourierActorRes> {
    const actor = await this.actors.getOrCreate(request.courierId, SampleNames.courierActorType, request);
    const active = this.directory.require(request.courierId);
    const actorRef = actorRefForMessage(actor);
    active.setActorRef(actorRef);
    return { courierId: request.courierId, actor: actorRef };
  }
}

@zlinkEntrySpotActorSendHandler({
  entrySpot: () => CourierEntrySpot,
  actor: () => CourierActor,
  packetName: PacketNames.offerDelivery
})
class CourierActorOfferHandler {
  async handle(_spot: CourierEntrySpot, actor: CourierActor, _context: unknown, request: OfferDeliveryMsg): Promise<void> {
    await actor.offer(request);
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
    await actor.decide(decision);
  }
}

export {
  OfferDeliveryEntrySpotHandler,
  EnsureCourierActorHandler,
  CourierActorOfferHandler,
  CourierActorBindHandler,
  CourierActorSessionBindHandler,
  CourierActorDecisionHandler
};
