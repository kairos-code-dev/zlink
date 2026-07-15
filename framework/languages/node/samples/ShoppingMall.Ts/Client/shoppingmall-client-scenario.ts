import { OrderStatuses } from '../Shared/Contracts/messages';
import type { ZLinkHttpClient } from '@zlink-systems/http-client';
import type {
  ContinueOrderWorkflowRes,
  GetOrderStateRes,
  OrderState,
  RebuildOrderProjectionRes,
  ServerAssertionRes,
  StartOrderReq,
  StartOrderRes,
  VerifyExpectedVersionFenceRes
} from '../Shared/Contracts/messages';

class ShoppingMallClientScenario {
  async run(apiA: ZLinkHttpClient, apiB: ZLinkHttpClient, signal?: AbortSignal): Promise<void> {
    const seeded = await apiA.post('/self-check/seed').asyncRaw();
    ensure(() => seeded.status >= 200 && seeded.status < 300);

    const successReq = startOrderReq('cart-success', 'addr-home', 'pm-ok', 'order-success-001');
    const success = await apiA.post('/orders/start').body(successReq).fetch<StartOrderRes>();
    ensure(() => success.status === OrderStatuses.Created);
    ensure(() => success.orderId.length > 0);

    const created = await this.getOrder(apiA, success.orderId);
    ensure(() => this.isStartedOrConfirmed(created));
    ensure(() => created.shippingAddressId === successReq.shippingAddressId);

    const confirmed = await this.waitForStatus(apiA, success.orderId, OrderStatuses.Confirmed, signal);
    ensure(() => confirmed.reservationId === `reservation-${success.orderId}`);
    ensure(() => confirmed.paymentId === `payment-${success.orderId}`);
    ensure(() => confirmed.amount === 120);
    ensure(() => confirmed.currency === 'USD');
    console.log('shoppingmall-success=completed');

    const duplicate = await apiB.post('/orders/start').body(successReq).fetch<StartOrderRes>();
    ensure(() => duplicate.orderId === success.orderId);
    console.log('shoppingmall-idempotency=completed');

    const concurrentReq = startOrderReq('cart-success', 'addr-office', 'pm-ok', 'order-concurrent-001');
    const [concurrentA, concurrentB] = await Promise.all([
      apiA.post('/orders/start').body(concurrentReq).fetch<StartOrderRes>(),
      apiB.post('/orders/start').body(concurrentReq).fetch<StartOrderRes>()
    ]);
    ensure(() => concurrentA.orderId === concurrentB.orderId);
    const concurrentConfirmed = await this.waitForStatus(apiA, concurrentA.orderId, OrderStatuses.Confirmed, signal);
    ensure(() => concurrentConfirmed.status === OrderStatuses.Confirmed);
    console.log('shoppingmall-concurrent=completed');

    const pendingReq = startOrderReq('cart-success', 'addr-office', 'pm-ok', 'order-pending-001');
    const pendingHook = await apiA.post('/self-check/idempotency/pending')
      .body({ idempotencyKey: pendingReq.idempotencyKey, orderId: 'order-pending-0001', ownerInstanceId: 'api-a' })
      .asyncRaw();
    ensure(() => pendingHook.status >= 200 && pendingHook.status < 300);
    const pending = await apiB.post('/orders/start').body(pendingReq).fetch<StartOrderRes>();
    ensure(() => pending.orderId === 'order-pending-0001');
    ensure(() => pending.status === OrderStatuses.Created);
    const pendingCreated = await this.getOrder(apiA, pending.orderId);
    ensure(() => this.isStartedOrConfirmed(pendingCreated));
    ensure(() => pendingCreated.shippingAddressId === pendingReq.shippingAddressId);
    console.log('shoppingmall-pending=completed');

    const resumeReq = startOrderReq('cart-success', 'addr-home', 'pm-ok', 'order-resume-001');
    const inventoryReserved = await apiA.post('/self-check/workflow/inventory-reserved')
      .body(resumeReq)
      .fetch<StartOrderRes>();
    ensure(() => inventoryReserved.status === OrderStatuses.InventoryReserved);
    const resumed = await apiB.post(`/self-check/workflow/${inventoryReserved.orderId}/continue`)
      .fetch<ContinueOrderWorkflowRes>();
    ensure(() => resumed.state.status === OrderStatuses.Confirmed);
    ensure(() => resumed.state.reservationId === `reservation-${inventoryReserved.orderId}`);
    ensure(() => resumed.state.paymentId === `payment-${inventoryReserved.orderId}`);
    console.log('shoppingmall-resume=completed');

    const interruptedReq = startOrderReq('cart-success', 'addr-home', 'pm-ok', 'order-interrupted-001');
    const interrupted = await apiA.post('/self-check/workflow/inventory-effect')
      .body(interruptedReq)
      .fetch<StartOrderRes>();
    ensure(() => interrupted.status === OrderStatuses.Created);
    const interruptionRecovered = await apiB.post(`/self-check/workflow/${interrupted.orderId}/continue`)
      .fetch<ContinueOrderWorkflowRes>();
    ensure(() => interruptionRecovered.state.status === OrderStatuses.Confirmed);
    console.log('shoppingmall-effect-interruption=completed');

    const fence = await apiA.post(`/self-check/workflow/${success.orderId}/verify-fence`)
      .fetch<VerifyExpectedVersionFenceRes>();
    ensure(() => fence.rejected);
    ensure(() => fence.actualVersion > fence.expectedVersion);
    console.log('shoppingmall-version-fence=completed');

    const inventoryReq = startOrderReq('cart-inventory-fail', 'addr-home', 'pm-ok', 'order-inventory-001');
    const inventoryStarted = await apiA.post('/orders/start').body(inventoryReq).fetch<StartOrderRes>();
    const inventoryFailed = await this.waitForStatus(apiA, inventoryStarted.orderId, OrderStatuses.Failed, signal);
    ensure(() => inventoryFailed.reason?.toLowerCase().includes('inventory') === true);
    console.log('shoppingmall-inventory-failure=completed');

    const paymentReq = startOrderReq('cart-payment-fail', 'addr-home', 'pm-decline', 'order-payment-001');
    const paymentStarted = await apiB.post('/orders/start').body(paymentReq).fetch<StartOrderRes>();
    const paymentFailed = await this.waitForStatus(apiB, paymentStarted.orderId, OrderStatuses.Failed, signal);
    ensure(() => paymentFailed.reservationId !== undefined);
    ensure(() => paymentFailed.reason?.toLowerCase().includes('payment') === true);
    console.log('shoppingmall-payment-failure=completed');

    const deleteProjection = await apiA.post(`/self-check/projection/${success.orderId}/delete`).asyncRaw();
    ensure(() => deleteProjection.status >= 200 && deleteProjection.status < 300);
    const healedByContinue = await apiB.post(`/self-check/workflow/${success.orderId}/continue`)
      .fetch<ContinueOrderWorkflowRes>();
    ensure(() => healedByContinue.state.status === OrderStatuses.Confirmed);

    const deleteProjectionAgain = await apiA.post(`/self-check/projection/${success.orderId}/delete`).asyncRaw();
    ensure(() => deleteProjectionAgain.status >= 200 && deleteProjectionAgain.status < 300);
    const rebuilt = await apiA.post(`/self-check/projection/${success.orderId}/rebuild`)
      .fetch<RebuildOrderProjectionRes>();
    ensure(() => rebuilt.state.status === OrderStatuses.Confirmed);
    const rebuiltRead = await this.getOrder(apiB, success.orderId);
    ensure(() => rebuiltRead.status === OrderStatuses.Confirmed);
    console.log('shoppingmall-rebuild=completed');

    const delayedFirst = await this.getOrder(apiB, paymentStarted.orderId);
    const delayedSecond = await this.getOrder(apiA, paymentStarted.orderId);
    ensure(() => delayedFirst.status === delayedSecond.status);
    ensure(() => delayedSecond.status === OrderStatuses.Failed);
    console.log('shoppingmall-consistency=completed');

    const [scaleA, scaleB] = await Promise.all([
      apiA.post('/orders/start')
        .body(startOrderReq('cart-success', 'addr-office', 'pm-ok', 'order-scale-001'))
        .fetch<StartOrderRes>(),
      apiB.post('/orders/start')
        .body(startOrderReq('cart-success', 'addr-office', 'pm-ok', 'order-scale-002'))
        .fetch<StartOrderRes>()
    ]);
    const [scaleAConfirmed, scaleBConfirmed] = await Promise.all([
      this.waitForStatus(apiB, scaleA.orderId, OrderStatuses.Confirmed, signal),
      this.waitForStatus(apiA, scaleB.orderId, OrderStatuses.Confirmed, signal)
    ]);
    ensure(() => scaleAConfirmed.status === OrderStatuses.Confirmed);
    ensure(() => scaleBConfirmed.status === OrderStatuses.Confirmed);
    console.log('shoppingmall-scaleout=completed');

    const assertion = await apiA.post('/self-check/assert')
      .body({
        successfulOrderId: success.orderId,
        pendingRecoveredOrderId: pending.orderId,
        concurrentOrderId: concurrentA.orderId,
        resumedOrderId: inventoryReserved.orderId,
        interruptedOrderId: interrupted.orderId,
        inventoryFailureOrderId: inventoryStarted.orderId,
        paymentFailureOrderId: paymentStarted.orderId,
        scaleOutOrderIds: [scaleA.orderId, scaleB.orderId]
      })
      .fetch<ServerAssertionRes>();
    ensure(() => assertion.passed);
    console.log('shoppingmall-server-evidence=completed');
  }

  private async getOrder(api: ZLinkHttpClient, orderId: string): Promise<OrderState> {
    return (await api.get(`/orders/${orderId}`).fetch<GetOrderStateRes>()).state;
  }

  private async waitForStatus(
    api: ZLinkHttpClient,
    orderId: string,
    expectedStatus: string,
    signal?: AbortSignal
  ): Promise<OrderState> {
    let last: OrderState | undefined;
    for (let i = 0; i < 80; i++) {
      last = await this.getOrder(api, orderId);
      if (last.status === expectedStatus) {
        return last;
      }
      await delay(50, signal);
    }
    throw new Error(`Order '${orderId}' did not reach '${expectedStatus}' (last=${last?.status ?? 'none'}).`);
  }

  private isStartedOrConfirmed(state: OrderState): boolean {
    return state.status === OrderStatuses.Created ||
      state.status === OrderStatuses.InventoryReserved ||
      state.status === OrderStatuses.PaymentAuthorized ||
      state.status === OrderStatuses.Confirmed;
  }
}

function startOrderReq(
  cartId: string,
  shippingAddressId: string,
  paymentMethodId: string,
  idempotencyKey: string
): StartOrderReq {
  return { cartId, shippingAddressId, paymentMethodId, idempotencyKey };
}

function ensure(condition: () => boolean): void {
  if (!condition()) {
    throw new Error(`Ensure failed: ${condition.toString()}`);
  }
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

export { ShoppingMallClientScenario };
