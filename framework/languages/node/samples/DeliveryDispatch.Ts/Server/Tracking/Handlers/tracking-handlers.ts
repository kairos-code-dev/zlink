import { Inject } from '@nestjs/common';
import {
  ZLINK_ACTOR_CLIENT,
  zlinkRequestHandler
} from '@zlink-systems/nestjs';
import { DeliveryStatusChangedReq, DeliveryStatusUpdatedMsg, PacketNames } from '../../../Shared/Contracts/messages';
import { EvidenceStore } from '../../Configuration/evidence-store';
import type {
  ZLinkActorClient,
  ZLinkRequestContext,
  ZLinkRequestHandler,
  ZLinkLocationStore
} from '@zlink-systems/framework';
import type { DeliveryStatusChangedRes } from '../../../Shared/Contracts/messages';

@zlinkRequestHandler('tracking', PacketNames.deliveryStatusChanged)
class DeliveryStatusChangedHandler implements ZLinkRequestHandler<DeliveryStatusChangedReq, DeliveryStatusChangedRes> {
  constructor(
    @Inject(ZLINK_ACTOR_CLIENT) private readonly actors: ZLinkActorClient,
    @Inject('DELIVERYDISPATCH_LOCATION_STORE') private readonly locations: ZLinkLocationStore,
    private readonly evidence: EvidenceStore
  ) {}

  async handle(request: DeliveryStatusChangedReq, context: ZLinkRequestContext): Promise<DeliveryStatusChangedRes> {
    void context;
    this.evidence.append(request);
    const customerActor = (await this.locations.resolveActor({ actorId: request.customerId }))?.actorRef;
    if (customerActor === undefined) {
      throw new Error(`Customer actor '${request.customerId}' was not registered in the location store.`);
    }
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

export {
  DeliveryStatusChangedHandler
};
