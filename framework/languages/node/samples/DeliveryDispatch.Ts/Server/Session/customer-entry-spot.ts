import { CustomerActor } from './customer-actor';
import type { ZLinkEntrySpot, ZLinkEntrySpotContext } from '@zlink-systems/framework';

class CustomerEntrySpot implements ZLinkEntrySpot<CustomerActor> {
  readonly context!: ZLinkEntrySpotContext<CustomerActor>;
  async onActorJoin(): Promise<{ accepted: boolean }> { return { accepted: true }; }
  async onJoinedActor(): Promise<void> {}
  async onLeaveActor(): Promise<void> {}
}

export { CustomerEntrySpot };
