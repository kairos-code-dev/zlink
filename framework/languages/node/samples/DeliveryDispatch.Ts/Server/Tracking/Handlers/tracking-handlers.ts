import { Inject } from '@nestjs/common';
import {
  ZLINK_ACTOR_MANAGER,
  ZLINK_FANOUT_CLIENT,
  ZLINK_SPOT_MANAGER,
  zlinkRequestHandler
} from '@zlink-systems/nestjs';
import { SampleNames } from '../../../Shared/Configuration/sample-names';
import { PacketNames } from '../../../Shared/Contracts/messages';
import { EvidenceStore } from '../../Configuration/evidence-store';
import { DeliverySpotDirectory } from '../Spots/DeliveryTrackingSpot/delivery-spot-directory';
import { DeliveryTrackingSpot } from '../Spots/DeliveryTrackingSpot/delivery-tracking-spot';
import type {
  ZLinkActorManager,
  ZLinkFanoutClient,
  ZLinkRequestContext,
  ZLinkRequestHandler,
  ZLinkSpotManager
} from '@zlink-systems/framework';
import type {
  EnsureCustomerActorRes,
  SubscribeCustomerToDeliveryRes,
  DeliveryStatusRes,
  DeliveryStatusReq,
  DeliveryStatusNotify,
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
      actor: {
        nodeRid: String(actor.nodeRid),
        actorId: actor.actorId,
        generation: Number(actor.generation)
      }
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
    await this.spots.getOrCreate(DeliveryTrackingSpot, request.deliveryId, { deliveryId: request.deliveryId });
    await this.actors.getOrCreate(request.customerId, SampleNames.customerActorType);
    return { customerId: request.customerId, deliveryId: request.deliveryId };
  }
}

@zlinkRequestHandler('tracking', PacketNames.deliveryStatusChanged)
class DeliveryStatusChangedHandler implements ZLinkRequestHandler<DeliveryStatusReq, DeliveryStatusRes> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager,
    @Inject(ZLINK_FANOUT_CLIENT) private readonly fanout: ZLinkFanoutClient,
    private readonly directory: DeliverySpotDirectory,
    private readonly evidence: EvidenceStore
  ) {}

  async handle(request: DeliveryStatusReq, context: ZLinkRequestContext): Promise<DeliveryStatusRes> {
    void context;
    await this.spots.getOrCreate(DeliveryTrackingSpot, request.deliveryId, { deliveryId: request.deliveryId });
    this.evidence.append(request);
    this.directory.require(request.deliveryId).record(request);
    const notify: DeliveryStatusNotify = {
      deliveryId: request.deliveryId,
      status: request.status,
      courierId: request.courierId,
      occurredAt: request.occurredAt
    };
    await this.publishStatus(request.deliveryId, notify);
    console.error(`deliverydispatch tracking: status delivery=${request.deliveryId} status=${request.status} courier=${request.courierId ?? '-'}`);
    return { deliveryId: request.deliveryId, status: request.status };
  }

  private async publishStatus(deliveryId: string, notify: DeliveryStatusNotify): Promise<void> {
    await this.fanout
      .publishToChannel(SampleNames.statusFanoutChannel, deliveryId, notify)
      .packetName(PacketNames.deliveryStatusNotify)
      .submit();
  }
}

export {
  DeliveryStatusChangedHandler,
  EnsureCustomerActorHandler,
  SubscribeCustomerToDeliveryHandler
};
