import { Inject, Injectable } from '@nestjs/common';
import { GameplayDomain } from '../Domain/gameplay-domain';
import { GameplayEventPublisher } from '../Infrastructure/ZLink/gameplay-event-publisher';
import { QuestProgressStore } from '../../Shared/Store/quest-progress-store';
import type {
  CollectItemReq,
  CollectItemRes,
  CompleteMissionReq,
  CompleteMissionRes,
  EnterAreaReq,
  EnterAreaRes,
  GameplayEventEnvelope,
  KillMonsterReq,
  KillMonsterRes,
  QuestProgress,
  SyncQuestProgressRes,
  UnlockFeatureReq,
  UnlockFeatureRes
} from '../../../Shared/Contracts/messages';

@Injectable()
class GameplayActionService {
  private readonly apiName = process.env.GAMEQUEST_API_NAME ?? 'api';

  constructor(
    @Inject(QuestProgressStore) private readonly store: QuestProgressStore,
    @Inject(GameplayEventPublisher) private readonly publisher: GameplayEventPublisher
  ) {}

  async killMonster(request: KillMonsterReq): Promise<{ response: KillMonsterRes; projection: QuestProgress[]; completedQuestId?: string }> {
    return await this.publishAndNotify(GameplayDomain.monsterKilled(
      request.playerId,
      request.monsterId,
      request.idempotencyKey,
      this.apiName
    ));
  }

  async collectItem(request: CollectItemReq): Promise<{ response: CollectItemRes; projection: QuestProgress[]; completedQuestId?: string }> {
    return await this.publishAndNotify(GameplayDomain.itemCollected(
      request.playerId,
      request.itemId,
      request.count,
      request.idempotencyKey,
      this.apiName
    ));
  }

  async completeMission(request: CompleteMissionReq): Promise<{ response: CompleteMissionRes; projection: QuestProgress[]; completedQuestId?: string }> {
    return await this.publishAndNotify(GameplayDomain.missionCompleted(
      request.playerId,
      request.missionId,
      request.idempotencyKey,
      this.apiName
    ));
  }

  async enterArea(request: EnterAreaReq): Promise<{ response: EnterAreaRes; projection: QuestProgress[]; completedQuestId?: string }> {
    return await this.publishAndNotify(GameplayDomain.areaEntered(
      request.playerId,
      request.areaId,
      request.idempotencyKey,
      this.apiName
    ));
  }

  async unlockFeature(request: UnlockFeatureReq): Promise<{ response: UnlockFeatureRes; projection: QuestProgress[]; completedQuestId?: string }> {
    return await this.publishAndNotify(GameplayDomain.featureUnlocked(
      request.playerId,
      request.featureId,
      request.idempotencyKey,
      this.apiName
    ));
  }

  async sync(playerId: string): Promise<SyncQuestProgressRes> {
    return { updatedQuests: this.store.syncProgress(playerId) };
  }

  private async publishAndNotify<TResponse extends { eventId: string }>(
    candidate: GameplayEventEnvelope
  ): Promise<{ response: TResponse; projection: QuestProgress[]; completedQuestId?: string }> {
    const stored = this.store.recordGameplayEvent(candidate);
    const applied = await this.publisher.request(stored);
    console.error(`gamequest api event routed api=${this.apiName} player=${stored.playerId} event=${stored.eventId} owner=${applied.applied}`);
    return {
      response: { eventId: stored.eventId } as TResponse,
      projection: applied.projection,
      completedQuestId: applied.completedQuestId
    };
  }
}

export { GameplayActionService };
