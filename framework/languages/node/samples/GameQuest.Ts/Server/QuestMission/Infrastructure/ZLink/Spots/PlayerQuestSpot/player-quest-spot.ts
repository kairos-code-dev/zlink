import type { ZLinkMessage, ZLinkSpot, ZLinkSpotContext, ZLinkSpotCreateResponse } from '@zlink-systems/framework';
import type { ApplyGameplayEventReq, ApplyGameplayEventRes, SyncQuestProgressReq, SyncQuestProgressRes } from '../../../../../../Shared/Contracts/messages';

class PlayerQuestSpot implements ZLinkSpot {
  readonly context!: ZLinkSpotContext;
  playerId = '';

  async onCreate(request: ZLinkMessage): Promise<ZLinkSpotCreateResponse> {
    const decoded = request.decode<{ playerId: string }>(Object as never);
    this.playerId = decoded.playerId;
    console.error(`gamequest player quest spot ready player=${this.playerId} spot=${this.context.spotRid}`);
    return { accepted: true };
  }
  async onActorJoin(): Promise<{ accepted: boolean }> { return { accepted: true }; }
  async onJoinedActor(): Promise<void> {}
  async onLeaveActor(): Promise<void> {}

  async handleApply(request: ApplyGameplayEventReq): Promise<ApplyGameplayEventRes> {
    void request;
    return { applied: false, projection: [] };
  }

  async handleSync(request: SyncQuestProgressReq): Promise<SyncQuestProgressRes> {
    void request;
    return { updatedQuests: [] };
  }

}

export { PlayerQuestSpot };
