import { Inject } from '@nestjs/common';
import { DeliveryStore } from '../delivery-store';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { CreateDeliveryReq, DeliveryCreated } from '../../../Shared/Contracts/messages';

class CreateDeliveryHandler implements ZLinkRequestHandler<CreateDeliveryReq, DeliveryCreated> {
  constructor(@Inject(DeliveryStore) private readonly deliveries: DeliveryStore) {}

  async handle(request: CreateDeliveryReq): Promise<DeliveryCreated> {
    return this.deliveries.createDelivery(
      request.deliveryId,
      request.customerId,
      request.pickupAddress,
      request.dropoffAddress
    );
  }
}

export {
  CreateDeliveryHandler
};
