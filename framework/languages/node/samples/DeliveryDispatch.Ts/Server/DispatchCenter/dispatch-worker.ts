import { Inject, Injectable, type OnModuleDestroy, type OnModuleInit } from '@nestjs/common';
import { ZLINK_ACTOR_CLIENT, ZLINK_CHANNEL_CLIENT, ZLINK_SPOT_HANDLE_RESOLVER, ZLINK_SPOT_OUTBOUND } from '@zlink-systems/nestjs';
import { courierActorNodeRid, SampleNames, SampleTimings } from '../../Shared/Configuration/sample-names';
import { actorRefFromMessage, deliveryStatusChanged, ensureCourierActor, offerDelivery } from '../../Shared/Contracts/messages';
import { DeliveryOfferStore } from './delivery-offer-store';
import type { ActorRef, SpotHandle, ZLinkActorClient, ZLinkChannelClient, ZLinkLocationStore, ZLinkSpotHandleResolver, ZLinkSpotOutbound } from '@zlink-systems/framework';
import type {
  AssignDeliveryMsg,
  DeliveryStatusChangedReq,
  EnsureCourierActorRes,
  OfferDeliveryResultMsg
} from '../../Shared/Contracts/messages';
import type { DeliveryOffer, DeliveryOfferStatus } from './delivery-offer-store';

const courierCandidates = ['courier-a', 'courier-b'] as const;

@Injectable()
class DispatchWorker implements OnModuleInit, OnModuleDestroy {
  private operations = Promise.resolve();
  private sweepTimer: NodeJS.Timeout | undefined;

  constructor(
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient,
    @Inject(ZLINK_ACTOR_CLIENT) private readonly actors: ZLinkActorClient,
    @Inject(ZLINK_SPOT_HANDLE_RESOLVER) private readonly spotHandles: ZLinkSpotHandleResolver,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly spotOutbound: ZLinkSpotOutbound,
    @Inject('DELIVERYDISPATCH_LOCATION_STORE') private readonly locations: ZLinkLocationStore,
    private readonly offers: DeliveryOfferStore
  ) {}

  onModuleInit(): void {
    this.sweepTimer = setInterval(() => this.schedule(() => this.sweepExpiredOffers()), SampleTimings.offerSweepInterval);
    this.sweepTimer.unref();
    this.schedule(() => this.sweepExpiredOffers());
  }

  onModuleDestroy(): void {
    if (this.sweepTimer !== undefined) clearInterval(this.sweepTimer);
  }

  assign(request: AssignDeliveryMsg): void {
    this.schedule(() => this.startOffer(request, 1));
  }

  recordResult(result: OfferDeliveryResultMsg): void {
    this.schedule(() => this.applyResult(result));
  }

  private schedule(operation: () => Promise<void>): void {
    this.operations = this.operations.then(operation).catch((error: unknown) => {
      console.error(`deliverydispatch dispatch: ${error instanceof Error ? error.message : String(error)}`);
    });
  }

  private async startOffer(request: AssignDeliveryMsg, attempt: number): Promise<void> {
    if (attempt < 1 || attempt > courierCandidates.length) {
      const previous = this.offers.get(request.deliveryId);
      if (previous !== undefined) this.saveStatus(previous, 'Failed');
      await this.publishStatus(deliveryStatusChanged(request.deliveryId, request.customerId, 'Failed'));
      return;
    }
    const courierId = courierCandidates[attempt - 1];

    const actor = await this.findOrEnsureActor(courierId);
    const offer: DeliveryOffer = {
      deliveryId: request.deliveryId,
      customerId: request.customerId,
      pickupAddress: request.pickupAddress,
      dropoffAddress: request.dropoffAddress,
      courierId,
      attempt,
      deadline: Date.now() + SampleTimings.offerDecisionTimeout,
      status: 'Offered'
    };
    this.offers.save(offer);
    await this.publishStatus(deliveryStatusChanged(
      request.deliveryId,
      request.customerId,
      attempt === 1 ? 'Assigned' : 'Reassigned',
      courierId
    ));
    await this.actors.sendToActor(
      SampleNames.routeMesh,
      actor,
      offerDelivery(courierId, request.deliveryId, attempt, request.pickupAddress, request.dropoffAddress)
    ).submit();
  }

  private async applyResult(result: OfferDeliveryResultMsg): Promise<void> {
    const current = this.offers.get(result.deliveryId);
    if (
      current === undefined ||
      current.status !== 'Offered' ||
      current.attempt !== result.attempt ||
      current.courierId !== result.courierId
    ) {
      console.error(
        `deliverydispatch dispatch: ignored stale decision delivery=${result.deliveryId} `
        + `courier=${result.courierId} attempt=${result.attempt}`
      );
      return;
    }

    if (result.accepted) {
      this.saveStatus(current, 'Accepted');
      await this.continueAcceptedDelivery(current);
      return;
    }

    this.saveStatus(current, 'Rejected');
    await this.startOffer(current, current.attempt + 1);
  }

  private async sweepExpiredOffers(): Promise<void> {
    const now = Date.now();
    for (const offer of this.offers.offered()) {
      if (offer.deadline > now) continue;
      this.saveStatus(offer, 'Expired');
      await this.startOffer(offer, offer.attempt + 1);
    }
  }

  private saveStatus(offer: DeliveryOffer, status: DeliveryOfferStatus): void {
    this.offers.save({ ...offer, status });
  }

  private async findOrEnsureActor(courierId: string): Promise<ActorRef> {
    const found = await this.locations.resolveActor({ meshName: SampleNames.routeMesh, actorId: courierId });
    if (found !== undefined) return found.actorRef;
    const entrySpot = await this.resolveEntrySpot(courierId);
    const ensured = await this.spotOutbound
      .requestToSpot(entrySpot, ensureCourierActor(courierId))
      .timeout(SampleTimings.requestTimeout)
      .submit<EnsureCourierActorRes>();
    return actorRefFromMessage(ensured.actor);
  }

  private async resolveEntrySpot(courierId: string): Promise<SpotHandle> {
    const handle = await this.spotHandles.resolveSpotHandle(
      SampleNames.routeMesh,
      courierActorNodeRid(courierId)
    );
    if (handle === undefined) throw new Error(`Courier entry spot was not found for '${courierId}'.`);
    return handle;
  }

  private async continueAcceptedDelivery(offer: DeliveryOffer): Promise<void> {
    for (const status of ['Accepted', 'PickedUp', 'Delivered'] as const) {
      await this.publishStatus(deliveryStatusChanged(
        offer.deliveryId,
        offer.customerId,
        status,
        offer.courierId
      ));
    }
  }

  private async publishStatus(status: DeliveryStatusChangedReq): Promise<void> {
    await this.channels.requestToChannel(
      SampleNames.routeMesh,
      SampleNames.trackingChannel,
      status
    ).submit();
  }
}

export { DispatchWorker };
