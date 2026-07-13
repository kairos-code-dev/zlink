import { zlinkSendHandler } from '@zlink-systems/nestjs';
import { PacketNames } from '../../../Shared/Contracts/messages';
import { DispatchWorkQueue } from '../dispatch-work-queue';
import type { ZLinkSendHandler, ZLinkSendContext } from '@zlink-systems/framework';
import type { AssignDeliveryMsg } from '../../../Shared/Contracts/messages';

@zlinkSendHandler('dispatch', PacketNames.assignDelivery)
class AssignDeliveryHandler implements ZLinkSendHandler<AssignDeliveryMsg> {
  constructor(private readonly queue: DispatchWorkQueue) {}

  async handle(request: AssignDeliveryMsg, context: ZLinkSendContext): Promise<void> {
    void context;
    this.queue.enqueue(request);
    console.error(`deliverydispatch dispatch: queued delivery=${request.deliveryId} customer=${request.customerId}`);
  }
}

export { AssignDeliveryHandler };
