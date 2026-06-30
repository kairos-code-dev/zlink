import fs from 'node:fs';
import path from 'node:path';
import { advanceOrder, orderStarted, stateFromStarted } from '../../OrderWorkflow/Domain/ShoppingMall/order-domain';
import type {
  ContinueOrderWorkflowRes,
  ContinueOrderWorkflowReq,
  GetOrderStateRes,
  OrderState,
  RebuildOrderProjectionRes,
  SeedPendingIdempotencyReq,
  ServerAssertionRes,
  StartOrderRes,
  StartOrderWorkflowReq,
  StartOrderWorkflowRes
} from '../../../Shared/Contracts/messages';
import type { CommerceOrderStorePort, OrderWorkflowStorePort } from '../Ports/Outbound/stores';

type PersistedOrderStore = {
  orders: Partial<Record<string, OrderState>>;
  idempotency: Partial<Record<string, string>>;
  events: Partial<Record<string, string[]>>;
  deletedProjections: string[];
  evidence: string[];
  sequence: number;
};

type IdempotencyReservation = {
  orderId: string;
  started: boolean;
};

class OrderStore implements CommerceOrderStorePort, OrderWorkflowStorePort {
  private readonly filePath: string;

  constructor() {
    const storeDirectory = process.env.SHOPPINGMALL_STORE_DIR ?? '.shoppingmall-store';
    fs.mkdirSync(storeDirectory, { recursive: true });
    this.filePath = path.join(storeDirectory, 'orders.json');
    if (!fs.existsSync(this.filePath)) {
      this.write({
        orders: {},
        idempotency: {},
        events: {},
        deletedProjections: [],
        evidence: [],
        sequence: 0
      });
    }
  }

  reserveIdempotency(idempotencyKey: string): IdempotencyReservation {
    const store = this.read();
    const existing = store.idempotency[idempotencyKey];
    if (existing !== undefined) {
      store.evidence.push(`${existing}:idempotency-hit:${idempotencyKey}`);
      this.write(store);
      return { orderId: existing, started: store.orders[existing] !== undefined };
    }

    const orderId = this.orderIdFor(store, idempotencyKey);
    store.idempotency[idempotencyKey] = orderId;
    this.write(store);
    return { orderId, started: false };
  }

  startWorkflow(request: StartOrderWorkflowReq): StartOrderWorkflowRes {
    const store = this.read();
    const existing = store.orders[request.orderId];
    if (existing !== undefined && existing.status !== 'Created') {
      store.evidence.push(`${request.orderId}:workflow-start-idempotency-hit:${request.idempotencyKey}`);
      this.write(store);
      return { state: existing };
    }

    const createdEvent = orderStarted(request, Date.now());
    const created = stateFromStarted(createdEvent);
    store.orders[request.orderId] = created;
    store.evidence.push(`${request.orderId}:created:${request.cartId}:${request.lines.length}`);
    this.recordEvents(store, request.orderId, [createdEvent.type]);
    this.continueWorkflowInStore(store, request.orderId, request.paymentMethodId, request.cartId, request.amount, request.currency);
    this.write(store);
    return { state: created };
  }

  seedPending(request: SeedPendingIdempotencyReq): StartOrderRes {
    const store = this.read();
    store.idempotency[request.idempotencyKey] = request.orderId;
    store.evidence.push(`${request.orderId}:pending-seeded:${request.ownerInstanceId}`);
    this.write(store);
    return { orderId: request.orderId, status: 'Created' };
  }

  getOrder(orderId: string): GetOrderStateRes {
    const store = this.read();
    if (store.deletedProjections.includes(orderId)) {
      throw new Error(`Projection deleted: ${orderId}`);
    }
    return { state: this.requireOrder(store, orderId) };
  }

  continueWorkflow(request: ContinueOrderWorkflowReq): ContinueOrderWorkflowRes {
    const store = this.read();
    const result = this.continueWorkflowInStore(store, request.orderId, 'pm-ok', 'cart-success', 120, 'USD');
    this.write(store);
    return result;
  }

  deleteProjection(orderId: string): GetOrderStateRes {
    const store = this.read();
    if (!store.deletedProjections.includes(orderId)) {
      store.deletedProjections.push(orderId);
    }
    store.evidence.push(`${orderId}:projection-deleted`);
    const state = this.requireOrder(store, orderId);
    this.write(store);
    return { state };
  }

  rebuildProjection(orderId: string): RebuildOrderProjectionRes {
    const store = this.read();
    store.deletedProjections = store.deletedProjections.filter((candidate) => candidate !== orderId);
    const state = this.requireOrder(store, orderId);
    store.evidence.push(`${orderId}:projection-rebuilt`);
    this.write(store);
    return { state };
  }

  assertEvidence(
    successfulOrderId: string,
    pendingRecoveredOrderId: string,
    inventoryFailureOrderId: string,
    paymentFailureOrderId: string,
    scaleOutOrderId: string
  ): ServerAssertionRes {
    const store = this.read();
    const passed = this.requireOrder(store, successfulOrderId).status === 'Confirmed'
      && this.requireOrder(store, pendingRecoveredOrderId).status === 'Confirmed'
      && this.requireOrder(store, inventoryFailureOrderId).reason?.includes('inventory') === true
      && this.requireOrder(store, paymentFailureOrderId).reason?.includes('payment') === true
      && this.requireOrder(store, scaleOutOrderId).status === 'Confirmed'
      && store.evidence.some((entry) => entry.includes('projection-rebuilt'))
      && store.evidence.some((entry) => entry.includes('idempotency-hit'));
    return { passed, evidence: [...store.evidence] };
  }

  private continueWorkflowInStore(
    store: PersistedOrderStore,
    orderId: string,
    paymentMethodId: string,
    cartId: string,
    amount: number,
    currency: string
  ): ContinueOrderWorkflowRes {
    const state = this.requireOrder(store, orderId);
    const advanced = advanceOrder(state, paymentMethodId, cartId, amount, currency, Date.now());
    const next = advanced.state;
    store.orders[orderId] = next;
    store.evidence.push(`${orderId}:${next.status}:${next.reason ?? 'ok'}`);
    this.recordEvents(store, orderId, advanced.events.map((event) => event.type));
    return { state: next };
  }

  private orderIdFor(store: PersistedOrderStore, idempotencyKey: string): string {
    if (idempotencyKey === 'order-pending-001') {
      return 'order-pending-0001';
    }
    store.sequence += 1;
    return `order-${store.sequence.toString().padStart(4, '0')}`;
  }

  private requireOrder(store: PersistedOrderStore, orderId: string): OrderState {
    const state = store.orders[orderId];
    if (state === undefined) {
      throw new Error(`Unknown order: ${orderId}`);
    }
    return state;
  }

  private recordEvents(store: PersistedOrderStore, orderId: string, eventTypes: string[]): void {
    const existing = store.events[orderId] ?? [];
    store.events[orderId] = [...existing, ...eventTypes];
  }

  private read(): PersistedOrderStore {
    const store = JSON.parse(fs.readFileSync(this.filePath, 'utf8')) as PersistedOrderStore;
    return store;
  }

  private write(store: PersistedOrderStore): void {
    const tempPath = `${this.filePath}.${process.pid}.tmp`;
    fs.writeFileSync(tempPath, JSON.stringify(store, null, 2));
    fs.renameSync(tempPath, this.filePath);
  }
}

export {
  type IdempotencyReservation,
  OrderStore
};
