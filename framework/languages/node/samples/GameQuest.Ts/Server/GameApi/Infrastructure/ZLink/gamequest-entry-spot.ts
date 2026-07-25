import { GameQuestPlayerActor } from './gamequest-player-actor';
import type {
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkActorMembership,
  ZLinkMessage,
  ZLinkSpotActorJoinResponse
} from '@zlink-systems/framework';

class GameQuestEntrySpot implements ZLinkEntrySpot<GameQuestPlayerActor> {
  readonly context!: ZLinkEntrySpotContext<GameQuestPlayerActor>;

  async onActorJoin(_actorId: string, _request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    return { accepted: true };
  }

  async onCreateActor(_actor: ZLinkActorMembership, _request: ZLinkMessage): Promise<void> {}
  async onJoinedActor(_actor: ZLinkActorMembership): Promise<void> {}
  async onLeaveActor(_actor: ZLinkActorMembership): Promise<void> {}
  async onDisconnectActor(_actor: ZLinkActorMembership): Promise<void> {}
}

export { GameQuestEntrySpot };
