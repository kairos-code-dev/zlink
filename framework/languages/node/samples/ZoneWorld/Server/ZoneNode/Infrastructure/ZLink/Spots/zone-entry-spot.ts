import type {
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkMessage,
  ZLinkSpotActorJoinResponse
} from '@zlink-systems/framework';
import { PlayerActor as PlayerActorClass } from '../Actors/player-actor';
import type { PlayerActor } from '../Actors/player-actor';
import { EntryEnterWorldHandler, EntryJoinWorldHandler } from '../Handlers/player-handlers';

class ZoneEntrySpot implements ZLinkEntrySpot<PlayerActor> {
  readonly context!: ZLinkEntrySpotContext<PlayerActor, ZoneEntrySpot>;

  configure(): void {
    this.context.handlers.addActorPacket(EntryJoinWorldHandler, PlayerActorClass);
    this.context.handlers.addActorPacket(EntryEnterWorldHandler, PlayerActorClass);
  }

  async onActorJoin(_actorId: string, _request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    return { accepted: true };
  }

  async onJoinedActor(_actor: PlayerActor): Promise<void> {}

  async onLeaveActor(_actor: PlayerActor): Promise<void> {}
}


export { ZoneEntrySpot };
