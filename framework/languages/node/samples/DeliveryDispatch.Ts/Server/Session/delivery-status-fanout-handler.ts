import { Inject, Injectable } from '@nestjs/common';
import { zlinkPublishHandler } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { PacketNames } from '../../Shared/Contracts/messages';
import { CustomerSessionDirectory } from './customer-session-directory';
import type { ZLinkPublishContext, ZLinkPublishHandler } from '@zlink-systems/framework';
import type { DeliveryStatusNotify } from '../../Shared/Contracts/messages';

@Injectable()
@zlinkPublishHandler(SampleNames.statusFanoutChannel, PacketNames.deliveryStatusNotify)
class DeliveryStatusFanoutHandler implements ZLinkPublishHandler<DeliveryStatusNotify> {
  constructor(@Inject(CustomerSessionDirectory) private readonly sessions: CustomerSessionDirectory) {}

  async handle(message: DeliveryStatusNotify, context: ZLinkPublishContext): Promise<void> {
    void context;
    console.error(`deliverydispatch session: fanout delivery=${message.deliveryId} status=${message.status}`);
    await this.sessions.publish(message);
  }
}

export { DeliveryStatusFanoutHandler };
