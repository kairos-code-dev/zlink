import { Inject } from '@nestjs/common';
import { CheckoutStore } from '../checkout-store';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { CheckoutState, StartCheckoutReq } from '../../../Shared/Contracts/messages';

class StartCheckoutHandler implements ZLinkRequestHandler<StartCheckoutReq, CheckoutState> {
  constructor(@Inject(CheckoutStore) private readonly checkout: CheckoutStore) {}

  async handle(request: StartCheckoutReq): Promise<CheckoutState> {
    return this.checkout.startCheckout(request.customerId);
  }
}

export {
  StartCheckoutHandler
};
