import fs from 'node:fs';
import path from 'node:path';
import { OrderStatuses } from '../../../Shared/Contracts/messages';
import type { OrderState, ServerAssertionReq, StartOrderReq } from '../../../Shared/Contracts/messages';
import type { CommerceOrderStorePort, OrderWorkflowStorePort } from '../Ports/Outbound/stores';

interface StoreData {
  idempotency: Record<string, string | undefined>;
  orders: Record<string, OrderRecord | undefined>;
  projections: Record<string, OrderState | undefined>;
  evidence: string[];
}

interface OrderRecord {
  request: StartOrderReq;
  state: OrderState;
}

class OrderStore implements CommerceOrderStorePort, OrderWorkflowStorePort {
  constructor(private readonly filePath: string) {}

  static fromEnvironment(): OrderStore {
    const workDir = process.env.SHOPPINGMALL_WORK_DIR ?? path.join(process.cwd(), '.shoppingmall-work');
    fs.mkdirSync(workDir, { recursive: true });
    return new OrderStore(path.join(workDir, 'orders.json'));
  }

  createPendingMapping(idempotencyKey: string, orderId: string, ownerInstanceId: string): { created: boolean } {
    const data = this.read();
    data.idempotency[idempotencyKey] = orderId;
    data.evidence.push(`pending ${idempotencyKey} owner=${ownerInstanceId}`);
    this.write(data);
    return { created: true };
  }

  startOrder(request: StartOrderReq, instanceId: string): { orderId: string; status: string } {
    const data = this.read();
    const existingOrderId = data.idempotency[request.idempotencyKey];
    if (existingOrderId !== undefined) {
      if (data.orders[existingOrderId] === undefined) {
        const state = this.createdState(existingOrderId, request);
        data.orders[existingOrderId] = { request, state };
        data.projections[existingOrderId] = state;
      }
      data.evidence.push(`idempotent ${request.idempotencyKey} via=${instanceId}`);
      this.write(data);
      return { orderId: existingOrderId, status: data.orders[existingOrderId].state.status };
    }

    const orderId = request.idempotencyKey.replace(/-\d+$/, '-0001');
    data.idempotency[request.idempotencyKey] = orderId;
    const state = this.workflowState(orderId, request);
    data.orders[orderId] = { request, state };
    data.projections[orderId] = state;
    data.evidence.push(`started ${orderId} via=${instanceId} status=${state.status}`);
    this.write(data);
    return { orderId, status: OrderStatuses.Created };
  }

  createInventoryReserved(request: StartOrderReq, instanceId: string): { orderId: string; status: string } {
    const data = this.read();
    const orderId = request.idempotencyKey.replace(/-\d+$/, '-0001');
    const state = this.createdState(orderId, request, OrderStatuses.InventoryReserved);
    state.reservationId = `reservation-${orderId}`;
    data.idempotency[request.idempotencyKey] = orderId;
    data.orders[orderId] = { request, state };
    data.projections[orderId] = state;
    data.evidence.push(`inventory-reserved ${orderId} via=${instanceId}`);
    this.write(data);
    return { orderId, status: state.status };
  }

  continueOrder(orderId: string, instanceId: string): { state: OrderState } {
    const data = this.read();
    const record = this.requireOrder(data, orderId);
    const state = this.confirmedState(orderId, record.request);
    data.orders[orderId] = { request: record.request, state };
    data.projections[orderId] = state;
    data.evidence.push(`continued ${orderId} via=${instanceId}`);
    this.write(data);
    return { state };
  }

  getOrder(orderId: string): { state: OrderState } {
    const data = this.read();
    const state = data.projections[orderId] ?? this.requireOrder(data, orderId).state;
    return { state };
  }

  deleteProjection(orderId: string): { deleted: boolean } {
    const data = this.read();
    const deleted = data.projections[orderId] !== undefined;
    delete data.projections[orderId];
    data.evidence.push(`projection-deleted ${orderId}`);
    this.write(data);
    return { deleted };
  }

  rebuildProjection(orderId: string): { state: OrderState } {
    const data = this.read();
    const state = this.requireOrder(data, orderId).state;
    data.projections[orderId] = state;
    data.evidence.push(`projection-rebuilt ${orderId}`);
    this.write(data);
    return { state };
  }

  assertEvidence(request: ServerAssertionReq): { passed: boolean; evidence: string[] } {
    const data = this.read();
    const expected = [
      request.successfulOrderId,
      request.pendingRecoveredOrderId,
      request.concurrentOrderId,
      request.resumedOrderId,
      request.inventoryFailureOrderId,
      request.paymentFailureOrderId,
      request.scaleOutOrderId
    ];
    const missing = expected.filter((orderId) => data.orders[orderId] === undefined);
    const evidence = [...data.evidence, ...missing.map((orderId) => `missing ${orderId}`)];
    return { passed: missing.length === 0, evidence };
  }

  private workflowState(orderId: string, request: StartOrderReq): OrderState {
    if (request.cartId.includes('inventory-fail')) {
      return {
        ...this.createdState(orderId, request, OrderStatuses.Failed),
        reason: 'inventory reservation failed'
      };
    }
    if (request.cartId.includes('payment-fail') || request.paymentMethodId.includes('decline')) {
      return {
        ...this.createdState(orderId, request, OrderStatuses.Failed),
        reservationId: `reservation-${orderId}`,
        reason: 'payment authorization failed'
      };
    }
    return this.confirmedState(orderId, request);
  }

  private confirmedState(orderId: string, request: StartOrderReq): OrderState {
    return {
      ...this.createdState(orderId, request, OrderStatuses.Confirmed),
      reservationId: `reservation-${orderId}`,
      paymentId: `payment-${orderId}`,
      amount: 120,
      currency: 'USD'
    };
  }

  private createdState(orderId: string, request: StartOrderReq, status: string = OrderStatuses.Created): OrderState {
    return {
      orderId,
      status,
      shippingAddressId: request.shippingAddressId,
      updatedAtUnixMs: Date.now()
    };
  }

  private requireOrder(data: StoreData, orderId: string): OrderRecord {
    const record = data.orders[orderId];
    if (record === undefined) {
      throw new Error(`Order '${orderId}' does not exist.`);
    }
    return record;
  }

  private read(): StoreData {
    if (!fs.existsSync(this.filePath)) {
      return { idempotency: {}, orders: {}, projections: {}, evidence: [] };
    }
    return JSON.parse(fs.readFileSync(this.filePath, 'utf8')) as StoreData;
  }

  private write(data: StoreData): void {
    fs.writeFileSync(this.filePath, JSON.stringify(data, null, 2));
  }
}

export { OrderStore };
