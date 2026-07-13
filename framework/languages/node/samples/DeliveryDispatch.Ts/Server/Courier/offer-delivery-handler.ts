import { Inject, Optional } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, ZLINK_ROUTE_CLIENT, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { courierActorNodeRid, SampleNames } from '../../Shared/Configuration/sample-names';
import { OfferDeliveryReq, PacketNames } from '../../Shared/Contracts/messages';
import { delay } from '../Configuration/timing';
import type { ZLinkActorManager, ZLinkRequestContext, ZLinkRequestHandler, ZLinkRouteClient } from '@zlink-systems/framework';
import type { OfferDeliveryRes } from '../../Shared/Contracts/messages';
import type { CourierOptions } from './courier-module';

@zlinkRequestHandler('courier-gateway', PacketNames.offerDelivery)
class OfferDeliveryHandler implements ZLinkRequestHandler<OfferDeliveryReq, OfferDeliveryRes> {
  constructor(@Inject(ZLINK_ROUTE_CLIENT) private readonly routes: ZLinkRouteClient) {}

  async handle(request: OfferDeliveryReq, context: ZLinkRequestContext): Promise<OfferDeliveryRes> {
    void context;
    return await this.routes
      .requestToNode(
        SampleNames.courierActorNodeRouteChannel,
        courierActorNodeRid(request.courierId),
        new OfferDeliveryReq(request.courierId, request.deliveryId, request.pickupAddress, request.dropoffAddress)
      )
      .timeout(3000)
      .submit<OfferDeliveryRes>();
  }
}

@zlinkRequestHandler('courier-actor-node', PacketNames.offerDelivery)
class OfferDeliveryActorNodeHandler implements ZLinkRequestHandler<OfferDeliveryReq, OfferDeliveryRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actors: ZLinkActorManager,
    @Optional() @Inject('DELIVERYDISPATCH_COURIER_OPTIONS') private readonly options?: CourierOptions
  ) {}

  async handle(request: OfferDeliveryReq, context: ZLinkRequestContext): Promise<OfferDeliveryRes> {
    void context;
    await this.actors.getOrCreate(request.courierId, SampleNames.courierActorType, request);
    if (this.shouldTimeout(request.deliveryId)) {
      console.error(`deliverydispatch courier-actor-node: no response delivery=${request.deliveryId} courier=${request.courierId}`);
      await delay(30000);
    }

    const accepted = this.mode().toLowerCase() !== 'reject';
    console.error(`deliverydispatch courier-actor-node: decision delivery=${request.deliveryId} courier=${request.courierId} accepted=${accepted}`);
    return {
      deliveryId: request.deliveryId,
      courierId: request.courierId,
      accepted,
      reason: accepted ? undefined : 'courier rejected'
    };
  }

  private shouldTimeout(deliveryId: string): boolean {
    const mode = this.mode().toLowerCase();
    return mode === 'timeout' || (mode === 'timeout-reassign' && deliveryId.toLowerCase().includes('reassign'));
  }

  private mode(): string {
    return this.options?.mode ?? 'accept';
  }
}

export {
  OfferDeliveryActorNodeHandler,
  OfferDeliveryHandler
};
