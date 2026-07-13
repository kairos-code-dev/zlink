import { Inject } from '@nestjs/common';
import {
  ZLINK_ACTOR_MANAGER,
  ZLINK_ACTOR_CLIENT,
  ZLINK_FANOUT_CLIENT,
  ZLINK_SPOT_MANAGER,
  zlinkEntrySpotActorSendHandler,
  zlinkRequestHandler
} from '@zlink-systems/nestjs';
import { ZLinkMessage } from '@zlink-systems/framework';
import { SampleNames } from '../../../Shared/Configuration/sample-names';
import { actorRefForMessage, DeliveryStatusNotify, DeliveryStatusReq, DeliveryStatusUpdatedMsg, PacketNames } from '../../../Shared/Contracts/messages';
import { EvidenceStore } from '../../Configuration/evidence-store';
import { DeliverySpotDirectory } from '../Spots/DeliveryTrackingSpot/delivery-spot-directory';
import { DeliveryTrackingSpot } from '../Spots/DeliveryTrackingSpot/delivery-tracking-spot';
import { CustomerEntrySpot } from '../Spots/EntrySpot/customer-entry-spot';
import { CustomerActor } from '../customer-actor';
import type {
  ZLinkActorManager,
  ZLinkActorClient,
  ZLinkActor,
  ZLinkFanoutClient,
  ZLinkRequestContext,
  ZLinkRequestHandler,
  ZLinkSpotActorSendContext,
  ZLinkEntrySpotActorSendHandler,
  ZLinkSpotManager
} from '@zlink-systems/framework';
import type {
  EnsureCustomerActorRes,
  SubscribeCustomerToDeliveryRes,
  DeliveryStatusRes,
  EnsureCustomerActorReq,
  SubscribeCustomerToDeliveryReq
} from '../../../Shared/Contracts/messages';

@zlinkRequestHandler('tracking', PacketNames.ensureCustomerActor)
class EnsureCustomerActorHandler implements ZLinkRequestHandler<EnsureCustomerActorReq, EnsureCustomerActorRes> {
  constructor(@Inject(ZLINK_ACTOR_MANAGER) private readonly actors: ZLinkActorManager) {}

  async handle(request: EnsureCustomerActorReq, context: ZLinkRequestContext): Promise<EnsureCustomerActorRes> {
    void context;
    console.error(`deliverydispatch tracking: ensure customer=${request.customerId}`);
    const actor = await this.actors.getOrCreate(request.customerId, SampleNames.customerActorType, request);
    return {
      customerId: request.customerId,
      actor: actorRefForMessage(actor)
    };
  }
}

@zlinkRequestHandler('tracking', PacketNames.subscribeCustomerToDelivery)
class SubscribeCustomerToDeliveryHandler implements ZLinkRequestHandler<SubscribeCustomerToDeliveryReq, SubscribeCustomerToDeliveryRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actors: ZLinkActorManager,
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager
  ) {}

  async handle(request: SubscribeCustomerToDeliveryReq, context: ZLinkRequestContext): Promise<SubscribeCustomerToDeliveryRes> {
    void context;
    console.error(`deliverydispatch tracking: subscribe delivery=${request.deliveryId} customer=${request.customerId}`);
    await this.spots.getOrCreate(DeliveryTrackingSpot, request.deliveryId, ZLinkMessage.from({ deliveryId: request.deliveryId }));
    await this.actors.getOrCreate(request.customerId, SampleNames.customerActorType);
    return { customerId: request.customerId, deliveryId: request.deliveryId };
  }
}

@zlinkRequestHandler('tracking', PacketNames.deliveryStatusChanged)
class DeliveryStatusChangedHandler implements ZLinkRequestHandler<DeliveryStatusReq, DeliveryStatusRes> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager,
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager,
    @Inject(ZLINK_ACTOR_CLIENT) private readonly actors: ZLinkActorClient
  ) {}

  async handle(request: DeliveryStatusReq, context: ZLinkRequestContext): Promise<DeliveryStatusRes> {
    void context;
    await this.spots.getOrCreate(DeliveryTrackingSpot, request.deliveryId, ZLinkMessage.from({ deliveryId: request.deliveryId }));
    const customerActor = await this.actorManager.getOrCreate(request.customerId, SampleNames.customerActorType);
    this.actors.sendToActor(customerActor, new DeliveryStatusUpdatedMsg(
      request.deliveryId,
      request.customerId,
      request.status,
      request.occurredAt,
      request.courierId
    )).submit();
    console.error(`deliverydispatch tracking: status delivery=${request.deliveryId} status=${request.status} courier=${request.courierId ?? '-'}`);
    return { deliveryId: request.deliveryId, status: request.status };
  }
}

@zlinkEntrySpotActorSendHandler({
  actor: () => CustomerActor,
  entrySpot: () => CustomerEntrySpot,
  packetName: PacketNames.deliveryStatusUpdated
})
class DeliveryStatusUpdatedHandler implements ZLinkEntrySpotActorSendHandler<unknown, ZLinkActor, DeliveryStatusUpdatedMsg> {
  constructor(
    @Inject(ZLINK_FANOUT_CLIENT) private readonly fanout: ZLinkFanoutClient,
    private readonly directory: DeliverySpotDirectory,
    private readonly evidence: EvidenceStore
  ) {}

  async handle(
    _spot: unknown,
    _actor: unknown,
    _context: ZLinkSpotActorSendContext,
    message: DeliveryStatusUpdatedMsg
  ): Promise<void> {
    const statusEvent = new DeliveryStatusReq(
      message.deliveryId,
      message.customerId,
      message.status,
      message.occurredAt,
      message.courierId
    );
    this.evidence.append(statusEvent);
    this.directory.require(message.deliveryId).record(statusEvent);
    const notify = new DeliveryStatusNotify(
      message.deliveryId,
      message.status,
      message.occurredAt,
      message.courierId
    );
    await this.publishStatus(message.deliveryId, notify);
  }

  private async publishStatus(deliveryId: string, notify: DeliveryStatusNotify): Promise<void> {
    await this.fanout
      .publish(SampleNames.statusFanoutChannel, deliveryId, notify)
      .submit();
  }
}

export {
  DeliveryStatusChangedHandler,
  DeliveryStatusUpdatedHandler,
  EnsureCustomerActorHandler,
  SubscribeCustomerToDeliveryHandler
};
