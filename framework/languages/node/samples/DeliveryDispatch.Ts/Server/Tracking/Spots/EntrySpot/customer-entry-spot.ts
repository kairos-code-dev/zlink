import type { ZLinkEntrySpot, ZLinkEntrySpotContext, ZLinkSpotActorJoinResponse } from '@zlink-systems/framework';
import { CustomerActor } from '../../customer-actor';

class CustomerEntrySpot implements ZLinkEntrySpot<CustomerActor> {
  readonly context!: ZLinkEntrySpotContext<CustomerActor>;

  async onActorJoin(actorId: string, request: unknown): Promise<ZLinkSpotActorJoinResponse> {
    void actorId;
    void request;
    return { accepted: true };
  }
}

export { CustomerEntrySpot };
