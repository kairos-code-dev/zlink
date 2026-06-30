import { Inject, Injectable } from '@nestjs/common';
import { CustomerSessionDirectory } from './customer-session-directory';
import type { ZLinkPublishContext, ZLinkPublishHandler } from '@zlink-systems/framework';
import type { DeliveryStatusNotify } from '../../Shared/Contracts/messages';

@Injectable()
class DeliveryStatusFanoutHandler implements ZLinkPublishHandler<DeliveryStatusNotify> {
  constructor(@Inject(CustomerSessionDirectory) private readonly sessions: CustomerSessionDirectory) {}

  async handle(message: DeliveryStatusNotify, context: ZLinkPublishContext): Promise<void> {
    void context;
    console.error(`deliverydispatch session: fanout delivery=${message.deliveryId} status=${message.status}`);
    await this.sessions.publish(message);
  }
}

export { DeliveryStatusFanoutHandler };
