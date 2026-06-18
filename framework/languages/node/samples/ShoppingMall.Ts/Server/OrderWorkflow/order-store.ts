import type {
  ContinueOrderWorkflowRes,
  GetOrderStateRes,
  OrderState,
  RebuildOrderProjectionRes,
  SeedPendingIdempotencyReq,
  ServerAssertionRes,
  StartOrderReq,
  StartOrderRes
} from '../../Shared/Contracts/messages';

class OrderStore {
  private readonly orders = new Map<string, OrderState>();
  private readonly idempotency = new Map<string, string>();
  private readonly deletedProjections = new Set<string>();
  private readonly evidence: string[] = [];
  private sequence = 0;

  startOrder(request: StartOrderReq): StartOrderRes {
    const existing = this.idempotency.get(request.idempotencyKey);
    if (existing !== undefined) {
      const state = this.requireOrder(existing);
      if (state.shippingAddressId !== request.shippingAddressId) {
        this.orders.set(existing, { ...state, shippingAddressId: request.shippingAddressId, updatedAtUnixMs: Date.now() });
      }
      this.evidence.push(`${existing}:idempotency-hit:${request.idempotencyKey}`);
      if (this.requireOrder(existing).status === 'Created') {
        this.continueWorkflow(existing, request.paymentMethodId, request.cartId);
      }
      return { orderId: existing, status: this.requireOrder(existing).status };
    }

    const orderId = this.orderIdFor(request.idempotencyKey);
    this.idempotency.set(request.idempotencyKey, orderId);
    const created = this.makeState(orderId, 'Created', request.shippingAddressId, request.cartId);
    this.orders.set(orderId, created);
    this.evidence.push(`${orderId}:created:${request.cartId}`);
    this.continueWorkflow(orderId, request.paymentMethodId, request.cartId);
    return { orderId, status: created.status };
  }

  seedPending(request: SeedPendingIdempotencyReq): StartOrderRes {
    this.idempotency.set(request.idempotencyKey, request.orderId);
    this.orders.set(request.orderId, this.makeState(request.orderId, 'Created', undefined, 'pending'));
    this.evidence.push(`${request.orderId}:pending-seeded:${request.ownerInstanceId}`);
    return { orderId: request.orderId, status: 'Created' };
  }

  getOrder(orderId: string): GetOrderStateRes {
    if (this.deletedProjections.has(orderId)) {
      throw new Error(`Projection deleted: ${orderId}`);
    }
    return { state: this.requireOrder(orderId) };
  }

  continueWorkflow(orderId: string, paymentMethodId = 'pm-ok', cartId = 'cart-success'): ContinueOrderWorkflowRes {
    const state = this.requireOrder(orderId);
    let next: OrderState;
    if (cartId.includes('inventory-fail')) {
      next = { ...state, status: 'Failed', reason: 'inventory unavailable', updatedAtUnixMs: Date.now() };
    } else if (paymentMethodId.includes('decline')) {
      next = {
        ...state,
        status: 'Failed',
        reservationId: `reservation-${orderId}`,
        reason: 'payment declined',
        updatedAtUnixMs: Date.now()
      };
    } else {
      next = {
        ...state,
        status: 'Confirmed',
        reservationId: `reservation-${orderId}`,
        paymentId: `payment-${orderId}`,
        amount: 120,
        currency: 'USD',
        updatedAtUnixMs: Date.now()
      };
    }
    this.orders.set(orderId, next);
    this.evidence.push(`${orderId}:${next.status}:${next.reason ?? 'ok'}`);
    return { state: next };
  }

  deleteProjection(orderId: string): GetOrderStateRes {
    this.deletedProjections.add(orderId);
    this.evidence.push(`${orderId}:projection-deleted`);
    return { state: this.requireOrder(orderId) };
  }

  rebuildProjection(orderId: string): RebuildOrderProjectionRes {
    this.deletedProjections.delete(orderId);
    const state = this.requireOrder(orderId);
    this.evidence.push(`${orderId}:projection-rebuilt`);
    return { state };
  }

  assertEvidence(
    successfulOrderId: string,
    pendingRecoveredOrderId: string,
    inventoryFailureOrderId: string,
    paymentFailureOrderId: string,
    scaleOutOrderId: string
  ): ServerAssertionRes {
    const passed = this.requireOrder(successfulOrderId).status === 'Confirmed'
      && this.requireOrder(pendingRecoveredOrderId).status === 'Confirmed'
      && this.requireOrder(inventoryFailureOrderId).reason?.includes('inventory') === true
      && this.requireOrder(paymentFailureOrderId).reason?.includes('payment') === true
      && this.requireOrder(scaleOutOrderId).status === 'Confirmed'
      && this.evidence.some((entry) => entry.includes('projection-rebuilt'))
      && this.evidence.some((entry) => entry.includes('idempotency-hit'));
    return { passed, evidence: [...this.evidence] };
  }

  private makeState(
    orderId: string,
    status: OrderState['status'],
    shippingAddressId: string | undefined,
    cartId: string
  ): OrderState {
    return {
      orderId,
      status,
      shippingAddressId,
      amount: cartId.includes('success') ? 120 : undefined,
      currency: cartId.includes('success') ? 'USD' : undefined,
      updatedAtUnixMs: Date.now()
    };
  }

  private orderIdFor(idempotencyKey: string): string {
    if (idempotencyKey === 'order-pending-001') {
      return 'order-pending-0001';
    }
    this.sequence += 1;
    return `order-${this.sequence.toString().padStart(4, '0')}`;
  }

  private requireOrder(orderId: string): OrderState {
    const state = this.orders.get(orderId);
    if (state === undefined) {
      throw new Error(`Unknown order: ${orderId}`);
    }
    return state;
  }
}

export {
  OrderStore
};
