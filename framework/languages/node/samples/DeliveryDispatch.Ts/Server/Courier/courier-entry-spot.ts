import type { ZLinkEntrySpot, ZLinkEntrySpotContext } from '@zlink-systems/framework';
import type { CourierActor } from './courier-actor';

class CourierEntrySpot implements ZLinkEntrySpot<CourierActor> {
  readonly context!: ZLinkEntrySpotContext<CourierActor>;
  async onActorJoin(): Promise<{ accepted: boolean }> { return { accepted: true }; }
  async onJoinedActor(): Promise<void> {}
  async onLeaveActor(): Promise<void> {}
}

export { CourierEntrySpot };
