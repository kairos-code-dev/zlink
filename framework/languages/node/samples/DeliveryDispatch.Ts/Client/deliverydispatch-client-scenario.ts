import { CourierDecisionMsg, bindCourierSession, PacketNames, subscribeDelivery } from '../Shared/Contracts/messages';
import type { BrowserHttpClient } from '../../browser-client-runtime';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';
import type {
  CreateDeliveryRes,
  BindCourierSessionRes,
  DeliveryStatusNotify,
  OfferDeliveryNotify,
  ServerAssertionRes,
  SubscribeDeliveryRes
} from '../Shared/Contracts/messages';

class DeliveryDispatchClientScenario {
  async run(
    http: BrowserHttpClient,
    createCustomer: () => ZlinkStreamConnector,
    createCourierA: () => ZlinkStreamConnector,
    createCourierB: () => ZlinkStreamConnector,
    signal?: AbortSignal
  ): Promise<void> {
    let customer = createCustomer();
    let courierA = createCourierA();
    let courierB = createCourierB();
    try {
      await this.connect(customer, courierA, courierB, signal);
      const firstActorA = await this.bindCourier(courierA, 'courier-a', 'courier-node-1', signal);
      const firstActorB = await this.bindCourier(courierB, 'courier-b', 'courier-node-2', signal);
      await this.runSuccessfulDelivery(http, customer, courierA, signal);

      await Promise.all([customer.close(), courierA.close(), courierB.close()]);
      customer = createCustomer();
      courierA = createCourierA();
      courierB = createCourierB();
      await this.connect(customer, courierA, courierB, signal);
      const reboundA = await this.bindCourier(courierA, 'courier-a', 'courier-node-1', signal);
      const reboundB = await this.bindCourier(courierB, 'courier-b', 'courier-node-2', signal);
      ensure(() => reboundA.actor.generation === firstActorA.actor.generation);
      ensure(() => reboundB.actor.generation === firstActorB.actor.generation);
      ensure(() => reboundA.sessionRoute !== firstActorA.sessionRoute);
      ensure(() => reboundB.sessionRoute !== firstActorB.sessionRoute);

      await this.runReassignedDelivery(http, customer, courierA, courierB, signal);
      await this.assertServerEvidence(http);
    } finally {
      await Promise.allSettled([customer.close(), courierA.close(), courierB.close()]);
    }
  }

  private async connect(
    customer: ZlinkStreamConnector,
    courierA: ZlinkStreamConnector,
    courierB: ZlinkStreamConnector,
    signal?: AbortSignal
  ): Promise<void> {
    await Promise.all([customer.connect(signal), courierA.connect(signal), courierB.connect(signal)]);
  }

  private async bindCourier(
    courier: ZlinkStreamConnector,
    courierId: string,
    expectedNodeRid: string,
    signal?: AbortSignal
  ): Promise<BindCourierSessionRes> {
    const bound = await courier.request(bindCourierSession(courierId), Object)
      .packetName(PacketNames.bindCourierSession)
      .submit<BindCourierSessionRes>(signal);
    ensure(() => bound.courierId === courierId);
    ensure(() => bound.actor.actorId === courierId);
    ensure(() => bound.actor.nodeRid === expectedNodeRid);
    ensure(() => bound.sessionRoute.length > 0);
    return bound;
  }

  private async runSuccessfulDelivery(
    http: BrowserHttpClient,
    customer: ZlinkStreamConnector,
    courierA: ZlinkStreamConnector,
    signal?: AbortSignal
  ): Promise<void> {
    const deliveryId = 'delivery-success';
    const offerA = courierA.waitFor<OfferDeliveryNotify>(PacketNames.offerDeliveryNotify)
      .where((message) => message.payload.deliveryId === deliveryId)
      .submit(signal);
    const statuses = ['Assigned', 'Accepted', 'PickedUp', 'Delivered'] as const;
    const arrivals: string[] = [];
    const statusWaits = statuses.map((status) => customer.waitFor<DeliveryStatusNotify>(PacketNames.deliveryStatusNotify)
      .where((message) => message.payload.deliveryId === deliveryId && message.payload.status === status)
      .submit(signal)
      .then((message) => {
        arrivals.push(message.payload.status);
        return message;
      }));

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
    const offered = await offerA;
    ensure(() => offered.payload.courierId === 'courier-a');
    ensure(() => offered.payload.attempt === 1);
    courierA.send(new CourierDecisionMsg(deliveryId, 'courier-a', offered.payload.attempt, true))
      .packetName(PacketNames.courierDecision)
      .submit();
    const notifications = await Promise.all(statusWaits);
    ensure(() => arrivals.join(',') === statuses.join(','));
    ensure(() => notifications.every((message) => message.payload.courierId === 'courier-a'));
  }

  private async runReassignedDelivery(
    http: BrowserHttpClient,
    customer: ZlinkStreamConnector,
    courierA: ZlinkStreamConnector,
    courierB: ZlinkStreamConnector,
    signal?: AbortSignal
  ): Promise<void> {
    const deliveryId = 'delivery-reassign';
    const timedOutOffer = courierA.waitFor<OfferDeliveryNotify>(PacketNames.offerDeliveryNotify)
      .where((message) => message.payload.deliveryId === deliveryId)
      .submit(signal);
    const reassignedOffer = courierB.waitFor<OfferDeliveryNotify>(PacketNames.offerDeliveryNotify)
      .where((message) => message.payload.deliveryId === deliveryId)
      .submit(signal);
    const statuses = ['Assigned', 'Reassigned', 'Accepted', 'PickedUp', 'Delivered'] as const;
    const arrivals: string[] = [];
    const statusWaits = statuses.map((status) => customer.waitFor<DeliveryStatusNotify>(PacketNames.deliveryStatusNotify)
      .where((message) => message.payload.deliveryId === deliveryId && message.payload.status === status)
      .submit(signal)
      .then((message) => {
        arrivals.push(message.payload.status);
        return message;
      }));

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
    const firstOffer = await timedOutOffer;
    ensure(() => firstOffer.payload.courierId === 'courier-a');
    ensure(() => firstOffer.payload.attempt === 1);
    const secondOffer = await reassignedOffer;
    ensure(() => secondOffer.payload.courierId === 'courier-b');
    ensure(() => secondOffer.payload.attempt === 2);
    courierA.send(new CourierDecisionMsg(deliveryId, 'courier-a', firstOffer.payload.attempt, true))
      .packetName(PacketNames.courierDecision)
      .submit();
    courierB.send(new CourierDecisionMsg(deliveryId, 'courier-b', secondOffer.payload.attempt, true))
      .packetName(PacketNames.courierDecision)
      .submit();

    const notifications = await Promise.all(statusWaits);
    ensure(() => arrivals.join(',') === statuses.join(','));
    ensure(() => notifications[0]?.payload.courierId === 'courier-a');
    ensure(() => notifications.slice(1).every((message) => message.payload.courierId === 'courier-b'));
    console.log('deliverydispatch-reassignment=completed');
  }

  private async assertServerEvidence(http: BrowserHttpClient): Promise<void> {
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
