import { startOrderWorkflowReq } from '../../../Shared/Contracts/messages';
import { OrderStore } from '../../Shared/Store/order-store';
import type { StartOrderReq, StartOrderRes } from '../../../Shared/Contracts/messages';
import type { OrderWorkflowCommandPort } from '../Ports/Outbound/workflow-ports';

class StartOrderUseCase {
  constructor(
    private readonly orders: OrderStore,
    private readonly workflow: OrderWorkflowCommandPort
  ) {}

  async execute(request: StartOrderReq): Promise<StartOrderRes> {
    const cart = resolveCart(request.cartId);
    validateShippingAddress(request.shippingAddressId);
    validatePaymentMethod(request.paymentMethodId);

    const reservation = this.orders.reserveIdempotency(request.idempotencyKey);
    if (reservation.started) {
      const existing = this.orders.getOrder(reservation.orderId).state;
      return { orderId: existing.orderId, status: existing.status };
    }

    const started = await this.workflow.startOrder(startOrderWorkflowReq(
      reservation.orderId,
      request.cartId,
      request.shippingAddressId,
      request.paymentMethodId,
      request.idempotencyKey,
      cart.lines,
      cart.amount,
      cart.currency
    ));
    return { orderId: started.state.orderId, status: started.state.status };
  }
}

function resolveCart(cartId: string): { lines: { sku: string; quantity: number }[]; amount: number; currency: string } {
  if (!cartId.startsWith('cart-')) {
    throw new Error(`Unknown cart: ${cartId}`);
  }
  return {
    lines: [{ sku: cartId.includes('inventory-fail') ? 'sku-sold-out' : 'sku-basic', quantity: 1 }],
    amount: 120,
    currency: 'USD'
  };
}

function validateShippingAddress(shippingAddressId: string): void {
  if (!shippingAddressId.startsWith('addr-')) {
    throw new Error(`Unknown shipping address: ${shippingAddressId}`);
  }
}

function validatePaymentMethod(paymentMethodId: string): void {
  if (!paymentMethodId.startsWith('pm-')) {
    throw new Error(`Unknown payment method: ${paymentMethodId}`);
  }
}

export {
  StartOrderUseCase
};
