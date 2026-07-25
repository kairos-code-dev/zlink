import type {
  ZLinkActorMembership,
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkMessage,
  ZLinkSpotActorJoinResponse
} from '@zlink-systems/framework';
import type { CourierActor } from './courier-actor';

class CourierEntrySpot implements ZLinkEntrySpot<CourierActor> {
  readonly context!: ZLinkEntrySpotContext<CourierActor>;
  async onActorJoin(_actorId: string, _request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    return { accepted: true };
  }
  async onJoinedActor(_actor: ZLinkActorMembership): Promise<void> {}
  async onLeaveActor(_actor: ZLinkActorMembership): Promise<void> {}
  async onDisconnectActor(_actor: ZLinkActorMembership): Promise<void> {}
}

export { CourierEntrySpot };
