import type { CheckoutState, CheckoutStatus } from '../../Shared/Contracts/messages';

class CheckoutStore {
  private state?: CheckoutState;

  startCheckout(customerId: string): CheckoutState {
    this.state = {
      checkoutId: 'checkout-1',
      customerId,
      status: 'CartCreated'
    };
    return this.state;
  }

  advance(checkoutId: string, status: CheckoutStatus): CheckoutState {
    const state = this.requireState(checkoutId);
    state.status = status;
    return state;
  }

  private requireState(checkoutId: string): CheckoutState {
    if (this.state === undefined || this.state.checkoutId !== checkoutId) {
      throw new Error(`Unknown checkout: ${checkoutId}`);
    }
    return this.state;
  }
}

export {
  CheckoutStore
};
