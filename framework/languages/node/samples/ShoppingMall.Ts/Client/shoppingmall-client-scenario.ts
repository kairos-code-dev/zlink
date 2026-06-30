import type { ZLinkHttpClient } from '@zlink-systems/http-client';
import type {
  GetOrderStateRes,
  RebuildOrderProjectionRes,
  ServerAssertionRes,
  StartOrderReq,
  StartOrderRes
} from '../Shared/Contracts/messages';

class ShoppingMallClientScenario {
  async run(apiA: ZLinkHttpClient, apiB: ZLinkHttpClient, signal?: AbortSignal): Promise<void> {
    const successReq = startOrderBody('cart-success', 'addr-home', 'pm-ok', 'order-success-001');
    const success = await startOrder(apiA, successReq);
    ensure(success.status === 'Created', 'success order should start as Created');
    const confirmed = await waitForStatus(apiA, success.orderId, 'Confirmed', signal);
    ensure(confirmed.state.status === 'Confirmed', 'success order should be Confirmed');
    ensure(confirmed.state.reservationId !== undefined, 'success order should reserve inventory');
    ensure(confirmed.state.paymentId !== undefined, 'success order should authorize payment');
    ensure(confirmed.state.amount === 120, 'success order amount should come from cart');
    ensure(confirmed.state.currency === 'USD', 'success order currency should come from cart');

    const duplicate = await startOrder(apiB, successReq);
    ensure(duplicate.orderId === success.orderId, 'duplicate idempotency request should return original order');

    await apiA.post('/self-check/idempotency/pending')
      .body({ idempotencyKey: 'order-pending-001', orderId: 'order-pending-0001', ownerInstanceId: 'api-a' })
      .fetch<StartOrderRes>();
    const pending = await startOrder(apiB, startOrderBody('cart-success', 'addr-office', 'pm-ok', 'order-pending-001'));
    ensure(pending.orderId === 'order-pending-0001', 'pending idempotency should recover seeded order id');
    const pendingRecovered = await waitForStatus(apiA, pending.orderId, 'Confirmed', signal);
    ensure(pendingRecovered.state.status === 'Confirmed', 'pending recovered order should finish workflow');
    ensure(pendingRecovered.state.shippingAddressId === 'addr-office', 'pending recovered order should use latest request address');

    const inventory = await startOrder(apiA, startOrderBody('cart-inventory-fail', 'addr-home', 'pm-ok', 'order-inventory-001'));
    const inventoryFailed = await waitForStatus(apiA, inventory.orderId, 'Failed', signal);
    ensure(inventoryFailed.state.status === 'Failed', 'inventory failure order should fail');
    ensure(inventoryFailed.state.reason?.includes('inventory') === true, 'inventory failure reason should mention inventory');

    const payment = await startOrder(apiB, startOrderBody('cart-payment-fail', 'addr-home', 'pm-decline', 'order-payment-001'));
    const paymentFailed = await waitForStatus(apiB, payment.orderId, 'Failed', signal);
    ensure(paymentFailed.state.status === 'Failed', 'payment failure order should fail');
    ensure(paymentFailed.state.reservationId !== undefined, 'payment failure should keep reservation evidence');
    ensure(paymentFailed.state.reason?.includes('payment') === true, 'payment failure reason should mention payment');
    const paymentFailedFromApiA = await getOrder(apiA, payment.orderId);
    ensure(paymentFailedFromApiA.state.status === paymentFailed.state.status, 'payment failure should be visible through api-a');
    ensure(paymentFailedFromApiA.state.reason === paymentFailed.state.reason, 'payment failure reason should match across APIs');

    await apiA.post(`/self-check/projection/${success.orderId}/delete`).fetch<GetOrderStateRes>();
    const rebuilt = await apiA.post(`/self-check/projection/${success.orderId}/rebuild`)
      .fetch<RebuildOrderProjectionRes>();
    ensure(rebuilt.state.status === 'Confirmed', 'rebuilt projection should restore confirmed state');
    const rebuiltRead = await getOrder(apiB, success.orderId);
    ensure(rebuiltRead.state.status === 'Confirmed', 'rebuilt projection should be visible through api-b');

    const scale = await startOrder(apiB, startOrderBody('cart-success', 'addr-office', 'pm-ok', 'order-scale-001'));
    const scaleConfirmed = await waitForStatus(apiA, scale.orderId, 'Confirmed', signal);
    ensure(scaleConfirmed.state.status === 'Confirmed', 'scale-out order should be confirmed');

    const assertion = await apiA.post('/self-check/assert')
      .body({
        successfulOrderId: success.orderId,
        pendingRecoveredOrderId: pending.orderId,
        inventoryFailureOrderId: inventory.orderId,
        paymentFailureOrderId: payment.orderId,
        scaleOutOrderId: scale.orderId
      })
      .fetch<ServerAssertionRes>();
    ensure(assertion.passed, 'server evidence should pass');
    console.log('shoppingmall-server-evidence=completed');
  }
}

function startOrderBody(
  cartId: string,
  shippingAddressId: string,
  paymentMethodId: string,
  idempotencyKey: string
): StartOrderReq {
  return { cartId, shippingAddressId, paymentMethodId, idempotencyKey };
}

async function startOrder(api: ZLinkHttpClient, payload: StartOrderReq): Promise<StartOrderRes> {
  return await api.post('/orders/start')
    .body(payload)
    .fetch<StartOrderRes>();
}

async function getOrder(api: ZLinkHttpClient, orderId: string): Promise<GetOrderStateRes> {
  return await api.get(`/orders/${orderId}`).fetch<GetOrderStateRes>();
}

async function waitForStatus(
  api: ZLinkHttpClient,
  orderId: string,
  status: string,
  signal?: AbortSignal
): Promise<GetOrderStateRes> {
  while (signal?.aborted !== true) {
    const state = await getOrder(api, orderId);
    if (state.state.status === status) {
      return state;
    }
    await delay(100, signal);
  }
  throw new DOMException(`Timed out waiting for order ${orderId} status ${status}.`, 'TimeoutError');
}

function delay(ms: number, signal?: AbortSignal): Promise<void> {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(resolve, ms);
    signal?.addEventListener('abort', () => {
      clearTimeout(timer);
      reject(new DOMException('Operation aborted.', 'AbortError'));
    }, { once: true });
  });
}

function ensure(condition: boolean, message: string): void {
  if (!condition) {
    throw new Error(`ShoppingMall scenario assertion failed: ${message}`);
  }
}

export {
  ShoppingMallClientScenario
};
