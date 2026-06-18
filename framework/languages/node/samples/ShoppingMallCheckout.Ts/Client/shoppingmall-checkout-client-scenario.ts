import { authorizePaymentReq, confirmOrderReq, reserveInventoryReq, startCheckoutReq } from '../Shared/Contracts/messages';
import { SampleNames } from '../Shared/Configuration/sample-names';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import type { CheckoutState } from '../Shared/Contracts/messages';

class ShoppingMallCheckoutClientScenario {
  async run(channels: ZLinkChannelClient): Promise<void> {
    const cart = await channels
      .requestToChannel(SampleNames.checkoutChannel, startCheckoutReq('customer-1'))
      .submit<CheckoutState>();
    ensure(cart.status === 'CartCreated');

    const paid = await requestStep(channels, authorizePaymentReq(cart.checkoutId));
    ensure(paid.status === 'PaymentAuthorized');

    const reserved = await requestStep(channels, reserveInventoryReq(cart.checkoutId));
    ensure(reserved.status === 'InventoryReserved');

    const confirmed = await requestStep(channels, confirmOrderReq(cart.checkoutId));
    ensure(confirmed.status === 'OrderConfirmed');
  }
}

async function requestStep(
  channels: ZLinkChannelClient,
  request: unknown
): Promise<CheckoutState> {
  return await channels
    .requestToChannel(SampleNames.checkoutChannel, request)
    .submit<CheckoutState>();
}

function ensure(condition: boolean): void {
  if (!condition) {
    throw new Error('ShoppingMallCheckout scenario assertion failed.');
  }
}

export {
  ShoppingMallCheckoutClientScenario
};
