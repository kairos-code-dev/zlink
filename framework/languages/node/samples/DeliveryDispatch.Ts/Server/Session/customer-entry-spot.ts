import { CustomerActor } from './customer-actor';
import type {
  ZLinkActorMembership,
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkMessage,
  ZLinkSpotActorJoinResponse
} from '@zlink-systems/framework';

class CustomerEntrySpot implements ZLinkEntrySpot<CustomerActor> {
  readonly context!: ZLinkEntrySpotContext<CustomerActor>;
  async onActorJoin(_actorId: string, _request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    return { accepted: true };
  }
  async onJoinedActor(_actor: ZLinkActorMembership): Promise<void> {}
  async onLeaveActor(_actor: ZLinkActorMembership): Promise<void> {}
  async onDisconnectActor(_actor: ZLinkActorMembership): Promise<void> {}
}

export { CustomerEntrySpot };
