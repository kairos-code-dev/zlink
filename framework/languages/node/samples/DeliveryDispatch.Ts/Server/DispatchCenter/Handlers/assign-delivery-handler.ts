import { Inject } from '@nestjs/common';
import { DeliveryStore } from '../delivery-store';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { AssignDeliveryReq, DeliveryRecord } from '../../../Shared/Contracts/messages';

class AssignDeliveryHandler implements ZLinkRequestHandler<AssignDeliveryReq, DeliveryRecord> {
  constructor(@Inject(DeliveryStore) private readonly deliveries: DeliveryStore) {}

  async handle(request: AssignDeliveryReq): Promise<DeliveryRecord> {
    return this.deliveries.assignCourier(request.deliveryId, request.courierId);
  }
}

export {
  AssignDeliveryHandler
};
