import { Inject } from '@nestjs/common';
import { DeliveryStore } from '../delivery-store';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { AdvanceDeliveryReq, DeliveryRecord } from '../../../Shared/Contracts/messages';

class AdvanceDeliveryHandler implements ZLinkRequestHandler<AdvanceDeliveryReq, DeliveryRecord> {
  constructor(@Inject(DeliveryStore) private readonly deliveries: DeliveryStore) {}

  async handle(request: AdvanceDeliveryReq): Promise<DeliveryRecord> {
    return this.deliveries.advance(request.deliveryId, request.status);
  }
}

export {
  AdvanceDeliveryHandler
};
