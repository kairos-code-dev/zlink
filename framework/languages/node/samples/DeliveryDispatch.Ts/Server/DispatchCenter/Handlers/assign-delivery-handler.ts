import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import { PacketNames } from '../../../Shared/Contracts/messages';
import { DispatchWorkQueue } from '../dispatch-work-queue';
import type { ZLinkRequestHandler, ZLinkRequestContext } from '@zlink-systems/framework';
import type { AssignDeliveryReq, AssignDeliveryRes } from '../../../Shared/Contracts/messages';

@zlinkRequestHandler('dispatch', PacketNames.assignDelivery)
class AssignDeliveryHandler implements ZLinkRequestHandler<AssignDeliveryReq, AssignDeliveryRes> {
  constructor(private readonly queue: DispatchWorkQueue) {}

  async handle(request: AssignDeliveryReq, context: ZLinkRequestContext): Promise<AssignDeliveryRes> {
    void context;
    this.queue.enqueue(request);
    console.error(`deliverydispatch dispatch: queued delivery=${request.deliveryId} customer=${request.customerId}`);
    return { deliveryId: request.deliveryId, courierId: 'pending', accepted: true };
  }
}

export { AssignDeliveryHandler };
