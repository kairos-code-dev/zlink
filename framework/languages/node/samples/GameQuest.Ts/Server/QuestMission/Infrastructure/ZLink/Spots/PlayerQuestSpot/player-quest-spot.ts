import type { ZLinkMessage, ZLinkSpot, ZLinkSpotContext, ZLinkSpotCreateResponse } from '@zlink-systems/framework';
import type { ApplyGameplayEventReq, ApplyGameplayEventRes, SyncQuestProgressReq, SyncQuestProgressRes } from '../../../../../../Shared/Contracts/messages';
import { PacketNames } from '../../../../../../Shared/Contracts/messages';

class PlayerQuestSpot implements ZLinkSpot {
  readonly context!: ZLinkSpotContext;
  playerId = '';

  async onCreate(request: ZLinkMessage): Promise<ZLinkSpotCreateResponse> {
    const decoded = request.decode<{ playerId: string }>(Object as never);
    this.playerId = decoded.playerId;
    console.error(`gamequest player quest spot ready player=${this.playerId} spot=${this.context.spotRid}`);
    return { accepted: true };
  }

  async handleApply(request: ApplyGameplayEventReq): Promise<ApplyGameplayEventRes> {
    void request;
    return { applied: false, projection: [] };
  }

  async handleSync(request: SyncQuestProgressReq): Promise<SyncQuestProgressRes> {
    void request;
    return { updatedQuests: [] };
  }

  packetName(): string {
    return PacketNames.applyGameplayEventReq;
  }
}

export { PlayerQuestSpot };
