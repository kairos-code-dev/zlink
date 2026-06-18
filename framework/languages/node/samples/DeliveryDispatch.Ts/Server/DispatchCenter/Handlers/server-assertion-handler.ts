import { Inject } from '@nestjs/common';
import { DeliveryStore } from '../delivery-store';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { ServerAssertionReq, ServerAssertionRes } from '../../../Shared/Contracts/messages';

class ServerAssertionHandler implements ZLinkRequestHandler<ServerAssertionReq, ServerAssertionRes> {
  constructor(@Inject(DeliveryStore) private readonly deliveries: DeliveryStore) {}

  async handle(request: ServerAssertionReq): Promise<ServerAssertionRes> {
    return this.deliveries.assertEvidence(request.successfulDeliveryId, request.reassignedDeliveryId);
  }
}

export {
  ServerAssertionHandler
};
