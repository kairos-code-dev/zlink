import { GameQuestPlayerActor } from './gamequest-player-actor';
import type {
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkMessage,
  ZLinkSpotActorJoinResponse
} from '@zlink-systems/framework';

class GameQuestEntrySpot implements ZLinkEntrySpot<GameQuestPlayerActor> {
  readonly context!: ZLinkEntrySpotContext<GameQuestPlayerActor>;

  async onActorJoin(_actorId: string, _request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    return { accepted: true };
  }

  async onCreateActor(_actor: GameQuestPlayerActor, _request: ZLinkMessage): Promise<void> {}
  async onJoinedActor(_actor: GameQuestPlayerActor): Promise<void> {}
  async onLeaveActor(_actor: GameQuestPlayerActor): Promise<void> {}
  async onDisconnectActor(actor: GameQuestPlayerActor): Promise<void> {
    await this.context.destroyActor(actor);
  }
}

export { GameQuestEntrySpot };
