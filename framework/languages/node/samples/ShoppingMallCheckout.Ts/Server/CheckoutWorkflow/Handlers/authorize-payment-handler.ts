import { Inject } from '@nestjs/common';
import { CheckoutStore } from '../checkout-store';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { CheckoutState, CheckoutStepReq } from '../../../Shared/Contracts/messages';

class AuthorizePaymentHandler implements ZLinkRequestHandler<CheckoutStepReq, CheckoutState> {
  constructor(@Inject(CheckoutStore) private readonly checkout: CheckoutStore) {}

  async handle(request: CheckoutStepReq): Promise<CheckoutState> {
    return this.checkout.advance(request.checkoutId, 'PaymentAuthorized');
  }
}

export {
  AuthorizePaymentHandler
};
