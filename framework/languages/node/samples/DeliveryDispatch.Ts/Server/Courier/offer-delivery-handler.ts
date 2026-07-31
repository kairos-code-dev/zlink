import { zlinkEntrySpotActorRequestHandler, zlinkEntrySpotActorSendHandler } from '@zlink-systems/nestjs';
import {
  PacketNames
} from '../../Shared/Contracts/messages';
import type {
  ZLinkMessageContext
} from '@zlink-systems/framework';
import type { BindCourierReq, BindCourierRes, BindCourierSessionReq, BindCourierSessionRes, CourierDecisionMsg, OfferDeliveryMsg } from '../../Shared/Contracts/messages';
import { CourierActor } from './courier-actor';
import { CourierEntrySpot } from './courier-entry-spot';

@zlinkEntrySpotActorSendHandler({
  entrySpot: () => CourierEntrySpot,
  actor: () => CourierActor,
  packetName: PacketNames.offerDelivery
})
class CourierActorOfferHandler {
  async handle(actor: CourierActor, _context: ZLinkMessageContext, request: OfferDeliveryMsg): Promise<void> {
    await actor.offer(request);
  }
}

@zlinkEntrySpotActorRequestHandler({
  entrySpot: () => CourierEntrySpot,
  actor: () => CourierActor,
  packetName: PacketNames.bindCourier
})
class CourierActorBindHandler {
  handle(actor: CourierActor, _context: ZLinkMessageContext, request: BindCourierReq): BindCourierRes {
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
    actor: CourierActor,
    _context: ZLinkMessageContext,
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
  async handle(actor: CourierActor, _context: ZLinkMessageContext, decision: CourierDecisionMsg): Promise<void> {
    await actor.decide(decision);
  }
}

export {
  CourierActorOfferHandler,
  CourierActorBindHandler,
  CourierActorSessionBindHandler,
  CourierActorDecisionHandler
};
