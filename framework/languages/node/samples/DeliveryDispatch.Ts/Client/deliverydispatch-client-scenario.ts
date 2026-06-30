import { PacketNames, subscribeDelivery } from '../Shared/Contracts/messages';
import type { ZLinkHttpClient } from '@zlink-systems/http-client';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';
import type {
  DeliveryCreated,
  DeliveryStatusNotify,
  ServerAssertionRes,
  SubscribeDeliveryAccepted
} from '../Shared/Contracts/messages';

class DeliveryDispatchClientScenario {
  async run(http: ZLinkHttpClient, customer: ZlinkStreamConnector, signal?: AbortSignal): Promise<void> {
    await customer.connect(signal);

    await this.runSuccessfulDelivery(http, customer, signal);
    await this.runReassignedDelivery(http, customer, signal);
    await this.assertServerEvidence(http);
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
      .submit<SubscribeDeliveryAccepted>(signal);
    ensure(() => subscribed.deliveryId === deliveryId);
    await delay(1000, signal);

    const created = await http.post('/deliveries')
      .body({
        deliveryId,
        customerId: 'customer-1',
        pickupAddress: 'Kitchen 12',
        dropoffAddress: 'Customer Lobby'
      })
      .fetch<DeliveryCreated>();
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
      .submit<SubscribeDeliveryAccepted>(signal);
    ensure(() => subscribed.deliveryId === deliveryId);
    await delay(1000, signal);

    const created = await http.post('/deliveries')
      .body({
        deliveryId,
        customerId: 'customer-1',
        pickupAddress: 'Kitchen 12',
        dropoffAddress: 'Customer Lobby'
      })
      .fetch<DeliveryCreated>();
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

function delay(ms: number, signal?: AbortSignal): Promise<void> {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(resolve, ms);
    signal?.addEventListener('abort', () => {
      clearTimeout(timer);
      reject(new DOMException('Operation aborted.', 'AbortError'));
    }, { once: true });
  });
}

export {
  DeliveryDispatchClientScenario
};
