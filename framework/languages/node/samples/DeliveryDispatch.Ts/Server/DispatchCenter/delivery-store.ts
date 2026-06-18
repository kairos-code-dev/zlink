import type {
  AdvanceDeliveryReq,
  DeliveryCreated,
  DeliveryRecord,
  DeliveryStatus,
  DeliveryStatusNotify,
  ServerAssertionRes,
  SubscribeDeliveryAccepted
} from '../../Shared/Contracts/messages';

class DeliveryStore {
  private readonly deliveries = new Map<string, DeliveryRecord>();
  private readonly subscriptions = new Set<string>();
  private readonly notifications: DeliveryStatusNotify[] = [];
  private readonly evidence: string[] = [];

  createDelivery(
    deliveryId: string,
    customerId: string,
    pickupAddress: string,
    dropoffAddress: string
  ): DeliveryCreated {
    if (this.deliveries.has(deliveryId)) {
      return { deliveryId };
    }
    const record: DeliveryRecord = {
      deliveryId,
      customerId,
      pickupAddress,
      dropoffAddress,
      status: 'Created'
    };
    this.deliveries.set(deliveryId, record);
    this.recordStatus(record);
    this.runDispatchPlan(record);
    return { deliveryId };
  }

  subscribeDelivery(deliveryId: string): SubscribeDeliveryAccepted {
    this.subscriptions.add(deliveryId);
    return { deliveryId };
  }

  assignCourier(deliveryId: string, courierId: string): DeliveryRecord {
    const record = this.requireDelivery(deliveryId);
    const next: DeliveryRecord = {
      ...record,
      courierId,
      status: 'Assigned'
    };
    this.deliveries.set(deliveryId, next);
    this.recordStatus(next);
    return next;
  }

  advance(deliveryId: string, status: AdvanceDeliveryReq['status']): DeliveryRecord {
    const record = this.requireDelivery(deliveryId);
    const next: DeliveryRecord = {
      ...record,
      status
    };
    this.deliveries.set(deliveryId, next);
    this.recordStatus(next);
    return next;
  }

  assertEvidence(successfulDeliveryId: string, reassignedDeliveryId: string): ServerAssertionRes {
    const successful = this.statusesFor(successfulDeliveryId);
    const reassigned = this.statusesFor(reassignedDeliveryId);
    const passed = includesInOrder(successful, ['Created', 'Assigned', 'Accepted', 'PickedUp', 'Delivered'])
      && includesInOrder(reassigned, ['Created', 'Assigned', 'Reassigned', 'Accepted', 'Delivered'])
      && this.courierFor(reassignedDeliveryId, 'Reassigned') === 'courier-b'
      && this.subscriptions.has(successfulDeliveryId)
      && this.subscriptions.has(reassignedDeliveryId);

    return {
      passed,
      evidence: [...this.evidence]
    };
  }

  private requireDelivery(deliveryId: string): DeliveryRecord {
    const record = this.deliveries.get(deliveryId);
    if (record === undefined) {
      throw new Error(`Unknown delivery: ${deliveryId}`);
    }
    return record;
  }

  private runDispatchPlan(record: DeliveryRecord): void {
    if (record.deliveryId.includes('reassign')) {
      this.assignCourier(record.deliveryId, 'courier-a');
      this.advanceWithCourier(record.deliveryId, 'Reassigned', 'courier-b');
      this.advanceWithCourier(record.deliveryId, 'Accepted', 'courier-b');
      this.advanceWithCourier(record.deliveryId, 'Delivered', 'courier-b');
      return;
    }

    this.assignCourier(record.deliveryId, 'courier-a');
    this.advanceWithCourier(record.deliveryId, 'Accepted', 'courier-a');
    this.advanceWithCourier(record.deliveryId, 'PickedUp', 'courier-a');
    this.advanceWithCourier(record.deliveryId, 'Delivered', 'courier-a');
  }

  private advanceWithCourier(
    deliveryId: string,
    status: Exclude<DeliveryStatus, 'Created'>,
    courierId: string
  ): DeliveryRecord {
    const record = this.requireDelivery(deliveryId);
    const next: DeliveryRecord = {
      ...record,
      courierId,
      status
    };
    this.deliveries.set(deliveryId, next);
    this.recordStatus(next);
    return next;
  }

  private recordStatus(record: DeliveryRecord): void {
    const notification: DeliveryStatusNotify = {
      deliveryId: record.deliveryId,
      status: record.status,
      courierId: record.courierId,
      occurredAt: new Date().toISOString()
    };
    this.notifications.push(notification);
    this.evidence.push(`${record.deliveryId}:${record.status}:${record.courierId ?? 'none'}`);
  }

  private statusesFor(deliveryId: string): DeliveryStatus[] {
    return this.notifications
      .filter((notification) => notification.deliveryId === deliveryId)
      .map((notification) => notification.status);
  }

  private courierFor(deliveryId: string, status: DeliveryStatus): string | undefined {
    return this.notifications.find((notification) => notification.deliveryId === deliveryId && notification.status === status)?.courierId;
  }
}

function includesInOrder(actual: DeliveryStatus[], expected: DeliveryStatus[]): boolean {
  let cursor = 0;
  for (const status of actual) {
    if (status === expected[cursor]) {
      cursor += 1;
    }
  }
  return cursor === expected.length;
}

export {
  DeliveryStore
};
