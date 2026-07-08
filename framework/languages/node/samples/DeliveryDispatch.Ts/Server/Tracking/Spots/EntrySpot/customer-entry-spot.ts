import type { ZLinkEntrySpot, ZLinkEntrySpotContext, ZLinkSpotActorJoinResponse } from '@zlink-systems/framework';
import { CustomerActor } from '../../customer-actor';

class CustomerEntrySpot implements ZLinkEntrySpot<CustomerActor> {
  readonly context!: ZLinkEntrySpotContext<CustomerActor>;

  async onActorJoin(actor: CustomerActor, request: unknown): Promise<ZLinkSpotActorJoinResponse> {
    void actor;
    void request;
    return { accepted: true };
  }
}

export { CustomerEntrySpot };
