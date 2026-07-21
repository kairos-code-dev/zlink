import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, zlinkEntrySpotActorRequestHandler, zlinkEntrySpotActorSendHandler, zlinkEntrySpotPacketHandler } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import {
  EnsureCourierActorReq,
  PacketNames,
  actorRefForMessage
} from '../../Shared/Contracts/messages';
import type {
  ZLinkActorManager,
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorSendContext,
  ZLinkSpotRequestHandler
} from '@zlink-systems/framework';
import type { BindCourierReq, BindCourierRes, BindCourierSessionReq, BindCourierSessionRes, CourierDecisionMsg, EnsureCourierActorRes, OfferDeliveryMsg } from '../../Shared/Contracts/messages';
import { CourierActor, CourierActorDirectory } from './courier-actor';
import { CourierEntrySpot } from './courier-entry-spot';

@zlinkEntrySpotPacketHandler({ entrySpot: () => CourierEntrySpot, packetName: PacketNames.ensureCourierActor })
class EnsureCourierActorHandler implements ZLinkSpotRequestHandler<CourierEntrySpot, EnsureCourierActorReq, EnsureCourierActorRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actors: ZLinkActorManager,
    private readonly directory: CourierActorDirectory
  ) {}
  async handle(_spot: CourierEntrySpot, request: EnsureCourierActorReq): Promise<EnsureCourierActorRes> {
    const actor = await this.actors.getOrCreate(
      SampleNames.routeMesh,
      request.courierId,
      SampleNames.courierActorType,
      request
    );
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
  async handle(actor: CourierActor, _context: ZLinkSpotActorSendContext, request: OfferDeliveryMsg): Promise<void> {
    await actor.offer(request);
  }
}

@zlinkEntrySpotActorRequestHandler({
  entrySpot: () => CourierEntrySpot,
  actor: () => CourierActor,
  packetName: PacketNames.bindCourier
})
class CourierActorBindHandler {
  handle(actor: CourierActor, _context: ZLinkSpotActorRequestContext, request: BindCourierReq): BindCourierRes {
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
    _context: ZLinkSpotActorRequestContext,
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
  async handle(actor: CourierActor, _context: ZLinkSpotActorSendContext, decision: CourierDecisionMsg): Promise<void> {
    await actor.decide(decision);
  }
}

export {
  EnsureCourierActorHandler,
  CourierActorOfferHandler,
  CourierActorBindHandler,
  CourierActorSessionBindHandler,
  CourierActorDecisionHandler
};
