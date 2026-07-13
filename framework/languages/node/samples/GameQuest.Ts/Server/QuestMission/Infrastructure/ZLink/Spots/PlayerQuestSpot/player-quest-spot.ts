import type { PlayerQuestAggregate } from '../../../../Domain/quest-domain';
import type { ZLinkMessage, ZLinkSpot, ZLinkSpotContext, ZLinkSpotCreateResponse } from '@zlink-systems/framework';

class PlayerQuestSpot implements ZLinkSpot {
  readonly context!: ZLinkSpotContext;
  playerId = '';
  private aggregate: PlayerQuestAggregate | undefined;

  async onCreate(request: ZLinkMessage): Promise<ZLinkSpotCreateResponse> {
    const decoded = request.decode<{ playerId: string }>(Object as never);
    this.playerId = decoded.playerId;
    this.aggregate = undefined;
    return { accepted: true };
  }

  ensureAggregate(load: () => PlayerQuestAggregate): PlayerQuestAggregate {
    this.aggregate ??= load();
    return this.aggregate;
  }

  replaceAggregate(aggregate: PlayerQuestAggregate): void {
    this.aggregate = aggregate;
  }

  async onActorJoin(): Promise<{ accepted: boolean }> { return { accepted: true }; }
  async onJoinedActor(): Promise<void> {}
  async onLeaveActor(): Promise<void> {}
}

export { PlayerQuestSpot };
