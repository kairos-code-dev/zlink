import { Inject, Injectable } from '@nestjs/common';
import { QuestProgressStore } from '../../Shared/Store/quest-progress-store';
import { QuestDomain } from '../Domain/quest-domain';
import type {
  ApplyGameplayEventRes,
  EnterAreaReq,
  GameplayEventEnvelope,
  GetGameplaySnapshotReq,
  GetGameplaySnapshotRes,
  SyncQuestProgressReq,
  SyncQuestProgressRes
} from '../../../Shared/Contracts/messages';

@Injectable()
class QuestEventProcessor {
  constructor(@Inject(QuestProgressStore) private readonly store: QuestProgressStore) {}

  async process(event: GameplayEventEnvelope): Promise<ApplyGameplayEventRes> {
    const result = this.store.applyGameplayEvent(event);
    console.error(`gamequest mission processed player=${event.playerId} event=${event.eventId} completed=${result.completedQuestId ?? '-'}`);
    return {
      applied: true,
      projection: result.projection,
      completedQuestId: result.completedQuestId
    };
  }

  enterArea(request: EnterAreaReq): void {
    QuestDomain.questIdForArea(request.areaId);
  }

  syncProgress(request: SyncQuestProgressReq): SyncQuestProgressRes {
    return { updatedQuests: this.store.syncProgress(request.playerId) };
  }

  readSnapshot(request: GetGameplaySnapshotReq): GetGameplaySnapshotRes {
    return this.store.readGameplaySnapshot(request.playerId);
  }
}

export { QuestEventProcessor };
