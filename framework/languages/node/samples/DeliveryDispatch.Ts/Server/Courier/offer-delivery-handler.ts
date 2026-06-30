import { Inject } from '@nestjs/common';
import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import { PacketNames } from '../../Shared/Contracts/messages';
import { delay } from '../Configuration/request-retry';
import type { ZLinkRequestContext, ZLinkRequestHandler } from '@zlink-systems/framework';
import type { OfferDelivery, OfferDeliveryResult } from '../../Shared/Contracts/messages';
import type { CourierOptions } from './courier-module';

@zlinkRequestHandler('courier', PacketNames.offerDelivery)
class OfferDeliveryHandler implements ZLinkRequestHandler<OfferDelivery, OfferDeliveryResult> {
  constructor(@Inject('DELIVERYDISPATCH_COURIER_OPTIONS') private readonly options: CourierOptions) {}

  async handle(request: OfferDelivery, context: ZLinkRequestContext): Promise<OfferDeliveryResult> {
    void context;
    if (this.shouldTimeout(request.deliveryId)) {
      console.error(`deliverydispatch courier: no response delivery=${request.deliveryId} courier=${this.options.courierId}`);
      await delay(30000);
    }

    const accepted = this.options.mode.toLowerCase() !== 'reject';
    console.error(`deliverydispatch courier: decision delivery=${request.deliveryId} courier=${this.options.courierId} accepted=${accepted}`);
    return {
      deliveryId: request.deliveryId,
      courierId: this.options.courierId,
      accepted,
      reason: accepted ? undefined : 'courier rejected'
    };
  }

  private shouldTimeout(deliveryId: string): boolean {
    const mode = this.options.mode.toLowerCase();
    return mode === 'timeout' || (mode === 'timeout-reassign' && deliveryId.toLowerCase().includes('reassign'));
  }
}

export { OfferDeliveryHandler };
