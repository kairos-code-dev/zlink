import { Inject } from '@nestjs/common';
import { DeliveryStore } from '../delivery-store';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { SubscribeDeliveryAccepted, SubscribeDeliveryReq } from '../../../Shared/Contracts/messages';

class SubscribeDeliveryHandler implements ZLinkRequestHandler<SubscribeDeliveryReq, SubscribeDeliveryAccepted> {
  constructor(@Inject(DeliveryStore) private readonly deliveries: DeliveryStore) {}

  async handle(request: SubscribeDeliveryReq): Promise<SubscribeDeliveryAccepted> {
    return this.deliveries.subscribeDelivery(request.deliveryId);
  }
}

export {
  SubscribeDeliveryHandler
};
