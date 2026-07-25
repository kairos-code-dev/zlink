import type {
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkActorMembership,
  ZLinkMessage,
  ZLinkSpotActorJoinResponse
} from '@zlink-systems/framework';
import type { PlayerActor } from '../Actors/player-actor';

class ZoneEntrySpot implements ZLinkEntrySpot<PlayerActor> {
  readonly context!: ZLinkEntrySpotContext<PlayerActor, ZoneEntrySpot>;

  async onActorJoin(_actorId: string, _request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    return { accepted: true };
  }

  async onJoinedActor(_actor: ZLinkActorMembership): Promise<void> {}

  async onLeaveActor(_actor: ZLinkActorMembership): Promise<void> {}

  async onDisconnectActor(_actor: ZLinkActorMembership): Promise<void> {}
}


export { ZoneEntrySpot };
