import {
  PacketNames
} from '../../../../Shared/Contracts/messages';
import { DeliveryStatusUpdatedHandler } from '../../Handlers/tracking-handlers';
import type { ZLinkEntrySpot, ZLinkEntrySpotContext, ZLinkSpotActorJoinResponse } from '@zlink-systems/framework';
import { CustomerActor } from '../../customer-actor';

class CustomerEntrySpot implements ZLinkEntrySpot<CustomerActor> {
  readonly context!: ZLinkEntrySpotContext<CustomerActor>;

  configure(): void {
    this.context.handlers.actorSend(PacketNames.deliveryStatusUpdated, DeliveryStatusUpdatedHandler, CustomerActor);
  }

  async onActorJoin(actor: CustomerActor, request: unknown): Promise<ZLinkSpotActorJoinResponse> {
    void actor;
    void request;
    return { accepted: true };
  }
}

export { CustomerEntrySpot };
