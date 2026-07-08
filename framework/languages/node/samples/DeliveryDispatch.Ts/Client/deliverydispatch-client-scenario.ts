import { bindCourierSession, PacketNames, subscribeDelivery } from '../Shared/Contracts/messages';
import type { ZLinkHttpClient } from '@zlink-systems/http-client';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';
import type {
  CreateDeliveryRes,
  BindCourierSessionRes,
  DeliveryStatusNotify,
  ServerAssertionRes,
  SubscribeDeliveryRes
} from '../Shared/Contracts/messages';

class DeliveryDispatchClientScenario {
  async run(
    http: ZLinkHttpClient,
    customer: ZlinkStreamConnector,
    courierA: ZlinkStreamConnector,
    courierB: ZlinkStreamConnector,
    signal?: AbortSignal
  ): Promise<void> {
    await Promise.all([
      customer.connect(signal),
      courierA.connect(signal),
      courierB.connect(signal)
    ]);
    await this.bindCourier(courierA, 'courier-a', 'courier-node-1', signal);
    await this.bindCourier(courierB, 'courier-b', 'courier-node-2', signal);

    await this.runSuccessfulDelivery(http, customer, signal);
    await this.runReassignedDelivery(http, customer, signal);
    await this.assertServerEvidence(http);
  }

  private async bindCourier(
    courier: ZlinkStreamConnector,
    courierId: string,
    expectedNodeRid: string,
    signal?: AbortSignal
  ): Promise<void> {
    const bound = await courier.request(bindCourierSession(courierId), Object)
      .packetName(PacketNames.bindCourierSession)
      .submit<BindCourierSessionRes>(signal);
    ensure(() => bound.courierId === courierId);
    ensure(() => bound.actor.actorId === courierId);
    ensure(() => bound.actor.nodeRid === expectedNodeRid);
    ensure(() => bound.sessionRoute.length > 0);
  }

  private async runSuccessfulDelivery(
    http: ZLinkHttpClient,
    customer: ZlinkStreamConnector,
    signal?: AbortSignal
  ): Promise<void> {
    const deliveryId = 'delivery-success';
    const assigned = customer.waitFor<DeliveryStatusNotify>(PacketNames.deliveryStatusNotify)
      .where((message) => message.payload.deliveryId === deliveryId && message.payload.status === 'Assigned')
      .submit(signal);
    const accepted = customer.waitFor<DeliveryStatusNotify>(PacketNames.deliveryStatusNotify)
      .where((message) => message.payload.deliveryId === deliveryId && message.payload.status === 'Accepted')
      .submit(signal);
    const pickedUp = customer.waitFor<DeliveryStatusNotify>(PacketNames.deliveryStatusNotify)
      .where((message) => message.payload.deliveryId === deliveryId && message.payload.status === 'PickedUp')
      .submit(signal);
    const delivered = customer.waitFor<DeliveryStatusNotify>(PacketNames.deliveryStatusNotify)
      .where((message) => message.payload.deliveryId === deliveryId && message.payload.status === 'Delivered')
      .submit(signal);

    const subscribed = await customer.request(subscribeDelivery(deliveryId), Object)
      .packetName(PacketNames.subscribeDelivery)
      .submit<SubscribeDeliveryRes>(signal);
    ensure(() => subscribed.deliveryId === deliveryId);

    const created = await http.post('/deliveries')
      .body({
        deliveryId,
        customerId: 'customer-1',
        pickupAddress: 'Kitchen 12',
        dropoffAddress: 'Customer Lobby'
      })
      .fetch<CreateDeliveryRes>();
    ensure(() => created.deliveryId === deliveryId);

    if ((await assigned).payload.courierId !== 'courier-a') {
      throw new Error('delivery-success Assigned must use courier-a.');
    }
    if ((await accepted).payload.courierId !== 'courier-a') {
      throw new Error('delivery-success Accepted must use courier-a.');
    }
    if ((await pickedUp).payload.courierId !== 'courier-a') {
      throw new Error('delivery-success PickedUp must use courier-a.');
    }
    if ((await delivered).payload.courierId !== 'courier-a') {
      throw new Error('delivery-success Delivered must use courier-a.');
    }
  }

  private async runReassignedDelivery(
    http: ZLinkHttpClient,
    customer: ZlinkStreamConnector,
    signal?: AbortSignal
  ): Promise<void> {
    const deliveryId = 'delivery-reassign';
    const assigned = customer.waitFor<DeliveryStatusNotify>(PacketNames.deliveryStatusNotify)
      .where((message) => message.payload.deliveryId === deliveryId && message.payload.status === 'Assigned')
      .submit(signal);
    const reassigned = customer.waitFor<DeliveryStatusNotify>(PacketNames.deliveryStatusNotify)
      .where((message) => message.payload.deliveryId === deliveryId && message.payload.status === 'Reassigned')
      .submit(signal);
    const accepted = customer.waitFor<DeliveryStatusNotify>(PacketNames.deliveryStatusNotify)
      .where((message) => message.payload.deliveryId === deliveryId && message.payload.status === 'Accepted')
      .submit(signal);
    const delivered = customer.waitFor<DeliveryStatusNotify>(PacketNames.deliveryStatusNotify)
      .where((message) => message.payload.deliveryId === deliveryId && message.payload.status === 'Delivered')
      .submit(signal);

    const subscribed = await customer.request(subscribeDelivery(deliveryId), Object)
      .packetName(PacketNames.subscribeDelivery)
      .submit<SubscribeDeliveryRes>(signal);
    ensure(() => subscribed.deliveryId === deliveryId);

    const created = await http.post('/deliveries')
      .body({
        deliveryId,
        customerId: 'customer-1',
        pickupAddress: 'Kitchen 12',
        dropoffAddress: 'Customer Lobby'
      })
      .fetch<CreateDeliveryRes>();
    ensure(() => created.deliveryId === deliveryId);

    if ((await assigned).payload.courierId !== 'courier-a') {
      throw new Error('delivery-reassign Assigned must use courier-a.');
    }
    if ((await reassigned).payload.courierId !== 'courier-b') {
      throw new Error('delivery-reassign Reassigned must use courier-b.');
    }
    if ((await accepted).payload.courierId !== 'courier-b') {
      throw new Error('delivery-reassign Accepted must use courier-b.');
    }
    if ((await delivered).payload.courierId !== 'courier-b') {
      throw new Error('delivery-reassign Delivered must use courier-b.');
    }
    console.log('deliverydispatch-reassignment=completed');
  }

  private async assertServerEvidence(http: ZLinkHttpClient): Promise<void> {
    const assertion = await http.post('/self-check/assert')
      .body({
        successfulDeliveryId: 'delivery-success',
        reassignedDeliveryId: 'delivery-reassign'
      })
      .fetch<ServerAssertionRes>();
    ensure(() => assertion.passed);
    console.log('deliverydispatch-server-evidence=completed');
  }
}

function ensure(condition: () => boolean): void {
  if (!condition()) {
    throw new Error(`Ensure failed: ${condition.toString()}`);
  }
}

export {
  DeliveryDispatchClientScenario
};
