import { Inject, Injectable, OnModuleInit } from '@nestjs/common';
import { ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import { SampleNames, SampleTimings } from '../../Shared/Configuration/sample-names';
import { deliveryStatusChanged, offerDelivery } from '../../Shared/Contracts/messages';
import { DispatchWorkQueue } from './dispatch-work-queue';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import type { AssignDeliveryReq, DeliveryStatusReq, OfferDeliveryRes } from '../../Shared/Contracts/messages';

@Injectable()
class DispatchWorker implements OnModuleInit {
  constructor(
    private readonly queue: DispatchWorkQueue,
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient
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

  private async dispatch(request: AssignDeliveryReq): Promise<void> {
    console.error(`deliverydispatch dispatch: assign delivery=${request.deliveryId} customer=${request.customerId}`);
    await new Promise((resolve) => setTimeout(resolve, 100));
    await this.publishStatus(deliveryStatusChanged(request.deliveryId, request.customerId, 'Assigned', 'courier-a'));

    const first = await this.tryOffer(request, 'courier-a');
    if (first.accepted) {
      await this.continueAcceptedDelivery(request.deliveryId, request.customerId, first.courierId);
      return;
    }

    await this.publishStatus(deliveryStatusChanged(request.deliveryId, request.customerId, 'Reassigned', 'courier-b'));
    const second = await this.requestChannel<OfferDeliveryRes>(
      SampleNames.courierRouteChannel,
      offerDelivery('courier-b', request.deliveryId, request.pickupAddress, request.dropoffAddress)
    );
    if (!second.accepted) {
      await this.publishStatus(deliveryStatusChanged(request.deliveryId, request.customerId, 'Failed', second.courierId));
      throw new Error(`Delivery '${request.deliveryId}' was rejected by all couriers.`);
    }

    await this.continueAcceptedDelivery(request.deliveryId, request.customerId, second.courierId);
  }

  private async tryOffer(request: AssignDeliveryReq, courierId: string): Promise<OfferDeliveryRes> {
    try {
      return await this.channels
        .requestToChannel(SampleNames.courierRouteChannel, offerDelivery(courierId, request.deliveryId, request.pickupAddress, request.dropoffAddress))
        .timeout(SampleTimings.dispatchTimeout)
        .submit<OfferDeliveryRes>();
    } catch (error) {
      console.error(`deliverydispatch dispatch: courier timeout delivery=${request.deliveryId} courier=${courierId}`);
      return { deliveryId: request.deliveryId, courierId, accepted: false, reason: error instanceof Error ? error.message : String(error) };
    }
  }

  private async continueAcceptedDelivery(deliveryId: string, customerId: string, courierId: string): Promise<void> {
    await this.publishStatus(deliveryStatusChanged(deliveryId, customerId, 'Accepted', courierId));
    await this.publishStatus(deliveryStatusChanged(deliveryId, customerId, 'PickedUp', courierId));
    await this.publishStatus(deliveryStatusChanged(deliveryId, customerId, 'Delivered', courierId));
  }

  private async publishStatus(status: DeliveryStatusReq): Promise<void> {
    await this.requestChannel(SampleNames.trackingChannel, status);
  }

  private async requestChannel<TReply>(channelName: string, request: unknown): Promise<TReply> {
    return await this.channels.requestToChannel(channelName, request).submit<TReply>();
  }
}

export { DispatchWorker };
