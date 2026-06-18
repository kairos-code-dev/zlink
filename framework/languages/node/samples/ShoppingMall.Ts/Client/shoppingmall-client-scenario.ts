import {
  deleteOrderProjectionReq,
  getOrderStateReq,
  rebuildOrderProjectionReq,
  seedPendingIdempotencyReq,
  serverAssertionReq,
  startOrderReq
} from '../Shared/Contracts/messages';
import { SampleNames } from '../Shared/Configuration/sample-names';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import type {
  GetOrderStateRes,
  RebuildOrderProjectionRes,
  ServerAssertionRes,
  StartOrderRes
} from '../Shared/Contracts/messages';

class ShoppingMallClientScenario {
  async run(channels: ZLinkChannelClient): Promise<void> {
    const success = await startOrder(channels, startOrderReq('cart-success', 'addr-home', 'pm-ok', 'order-success-001'));
    ensure(success.status === 'Created', 'success order should start as Created');
    const confirmed = await getOrder(channels, success.orderId);
    ensure(confirmed.state.status === 'Confirmed', 'success order should be Confirmed');
    ensure(confirmed.state.reservationId !== undefined, 'success order should reserve inventory');
    ensure(confirmed.state.paymentId !== undefined, 'success order should authorize payment');
    ensure(confirmed.state.amount === 120, 'success order amount should come from cart');
    ensure(confirmed.state.currency === 'USD', 'success order currency should come from cart');

    const duplicate = await startOrder(channels, startOrderReq('cart-success', 'addr-home', 'pm-ok', 'order-success-001'));
    ensure(duplicate.orderId === success.orderId, 'duplicate idempotency request should return original order');

    await request<StartOrderRes>(
      channels,
      seedPendingIdempotencyReq('order-pending-001', 'order-pending-0001', 'api-a')
    );
    const pending = await startOrder(channels, startOrderReq('cart-success', 'addr-office', 'pm-ok', 'order-pending-001'));
    ensure(pending.orderId === 'order-pending-0001', 'pending idempotency should recover seeded order id');
    const pendingRecovered = await getOrder(channels, pending.orderId);
    ensure(pendingRecovered.state.status === 'Confirmed', 'pending recovered order should finish workflow');
    ensure(pendingRecovered.state.shippingAddressId === 'addr-office', 'pending recovered order should use latest request address');

    const inventory = await startOrder(channels, startOrderReq('cart-inventory-fail', 'addr-home', 'pm-ok', 'order-inventory-001'));
    const inventoryFailed = await getOrder(channels, inventory.orderId);
    ensure(inventoryFailed.state.status === 'Failed', 'inventory failure order should fail');
    ensure(inventoryFailed.state.reason?.includes('inventory') === true, 'inventory failure reason should mention inventory');

    const payment = await startOrder(channels, startOrderReq('cart-payment-fail', 'addr-home', 'pm-decline', 'order-payment-001'));
    const paymentFailed = await getOrder(channels, payment.orderId);
    ensure(paymentFailed.state.status === 'Failed', 'payment failure order should fail');
    ensure(paymentFailed.state.reservationId !== undefined, 'payment failure should keep reservation evidence');
    ensure(paymentFailed.state.reason?.includes('payment') === true, 'payment failure reason should mention payment');

    await request<GetOrderStateRes>(channels, deleteOrderProjectionReq(success.orderId));
    const rebuilt = await request<RebuildOrderProjectionRes>(
      channels,
      rebuildOrderProjectionReq(success.orderId)
    );
    ensure(rebuilt.state.status === 'Confirmed', 'rebuilt projection should restore confirmed state');

    const scale = await startOrder(channels, startOrderReq('cart-success', 'addr-office', 'pm-ok', 'order-scale-001'));
    const scaleConfirmed = await getOrder(channels, scale.orderId);
    ensure(scaleConfirmed.state.status === 'Confirmed', 'scale-out order should be confirmed');

    const assertion = await request<ServerAssertionRes>(
      channels,
      serverAssertionReq(success.orderId, pending.orderId, inventory.orderId, payment.orderId, scale.orderId)
    );
    ensure(assertion.passed, 'server evidence should pass');
    console.log('shoppingmall-server-evidence=completed');
  }
}

async function startOrder(channels: ZLinkChannelClient, payload: unknown): Promise<StartOrderRes> {
  return await request<StartOrderRes>(channels, payload);
}

async function getOrder(channels: ZLinkChannelClient, orderId: string): Promise<GetOrderStateRes> {
  return await request<GetOrderStateRes>(channels, getOrderStateReq(orderId));
}

async function request<TResponse>(channels: ZLinkChannelClient, payload: unknown): Promise<TResponse> {
  return await channels
    .requestToChannel(SampleNames.workflowChannel, payload)
    .submit<TResponse>();
}

function ensure(condition: boolean, message: string): void {
  if (!condition) {
    throw new Error(`ShoppingMall scenario assertion failed: ${message}`);
  }
}

export {
  ShoppingMallClientScenario
};
