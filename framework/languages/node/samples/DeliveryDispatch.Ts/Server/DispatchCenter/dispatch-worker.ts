import { Inject, Injectable, OnModuleInit } from '@nestjs/common';
import { ZLINK_CHANNEL_CLIENT, ZLINK_ROUTE_CLIENT } from '@zlink-systems/nestjs';
import { courierActorNodeRid, SampleNames, SampleTimings } from '../../Shared/Configuration/sample-names';
import { actorRefFromMessage, deliveryStatusChanged, ensureCourierActor, offerDelivery } from '../../Shared/Contracts/messages';
import { DispatchWorkQueue } from './dispatch-work-queue';
import type { ActorRef, ZLinkChannelClient, ZLinkLocationStore, ZLinkRouteClient } from '@zlink-systems/framework';
import type { AssignDeliveryMsg, DeliveryStatusChangedReq, EnsureCourierActorRes, OfferDeliveryRes } from '../../Shared/Contracts/messages';

@Injectable()
class DispatchWorker implements OnModuleInit {
  constructor(
    private readonly queue: DispatchWorkQueue,
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient,
    @Inject(ZLINK_ROUTE_CLIENT) private readonly routes: ZLinkRouteClient,
    @Inject('DELIVERYDISPATCH_LOCATION_STORE') private readonly locations: ZLinkLocationStore
  ) {}

  onModuleInit(): void {
    void this.run();
  }

  private async run(): Promise<void> {
    for (;;) {
      const request = await this.queue.next();
      try {
        await this.dispatch(request);
      } catch (error) {
        console.error(`deliverydispatch dispatch: failed delivery=${request.deliveryId} error=${error instanceof Error ? error.message : String(error)}`);
      }
    }
  }

  private async dispatch(request: AssignDeliveryMsg): Promise<void> {
    console.error(`deliverydispatch dispatch: assign delivery=${request.deliveryId} customer=${request.customerId}`);
    const first = await this.tryOffer(request, 'courier-a');
    await this.publishStatus(deliveryStatusChanged(request.deliveryId, request.customerId, 'Assigned', 'courier-a'));
    if (first.accepted) {
      await this.continueAcceptedDelivery(request.deliveryId, request.customerId, first.courierId);
      return;
    }

    await this.publishStatus(deliveryStatusChanged(request.deliveryId, request.customerId, 'Reassigned', 'courier-b'));
    const second = await this.requestOffer(request, 'courier-b');
    if (!second.accepted) {
      await this.publishStatus(deliveryStatusChanged(request.deliveryId, request.customerId, 'Failed', second.courierId));
      throw new Error(`Delivery '${request.deliveryId}' was rejected by all couriers.`);
    }

    await this.continueAcceptedDelivery(request.deliveryId, request.customerId, second.courierId, false);
  }

  private async tryOffer(request: AssignDeliveryMsg, courierId: string): Promise<OfferDeliveryRes> {
    try {
      await this.findOrEnsureActor(courierId);
      return await this.routes
        .requestToNode(
          SampleNames.courierActorNodeRouteChannel,
          courierActorNodeRid(courierId),
          offerDelivery(courierId, request.deliveryId, request.pickupAddress, request.dropoffAddress)
        )
        .timeout(SampleTimings.dispatchTimeout)
        .submit<OfferDeliveryRes>();
    } catch (error) {
      console.error(`deliverydispatch dispatch: courier timeout delivery=${request.deliveryId} courier=${courierId}`);
      return { deliveryId: request.deliveryId, courierId, accepted: false, reason: error instanceof Error ? error.message : String(error) };
    }
  }

  private async requestOffer(request: AssignDeliveryMsg, courierId: string): Promise<OfferDeliveryRes> {
    await this.findOrEnsureActor(courierId);
    return await this.routes
      .requestToNode(
        SampleNames.courierActorNodeRouteChannel,
        courierActorNodeRid(courierId),
        offerDelivery(courierId, request.deliveryId, request.pickupAddress, request.dropoffAddress)
      )
      .timeout(SampleTimings.requestTimeout)
      .submit<OfferDeliveryRes>();
  }

  private async findOrEnsureActor(courierId: string): Promise<ActorRef> {
    const found = await this.locations.resolveActor({ actorId: courierId });
    if (found !== undefined) return found.actorRef;
    const ensured = await this.routes
      .requestToNode(
        SampleNames.courierActorNodeRouteChannel,
        courierActorNodeRid(courierId),
        ensureCourierActor(courierId)
      )
      .timeout(SampleTimings.requestTimeout)
      .submit<EnsureCourierActorRes>();
    return actorRefFromMessage(ensured.actor);
  }

  private async continueAcceptedDelivery(deliveryId: string, customerId: string, courierId: string, includePickedUp = true): Promise<void> {
    await this.publishStatus(deliveryStatusChanged(deliveryId, customerId, 'Accepted', courierId));
    if (includePickedUp) {
      await this.publishStatus(deliveryStatusChanged(deliveryId, customerId, 'PickedUp', courierId));
    }
    await this.publishStatus(deliveryStatusChanged(deliveryId, customerId, 'Delivered', courierId));
  }

  private async publishStatus(status: DeliveryStatusChangedReq): Promise<void> {
    await this.requestChannel(SampleNames.trackingChannel, status);
  }

  private async requestChannel<TReply>(channelName: string, request: unknown): Promise<TReply> {
    return await this.channels.requestToChannel(channelName, request).submit<TReply>();
  }
}

export { DispatchWorker };
