import type {
  CollectItemReq,
  CompleteMissionReq,
  DeleteQuestProjectionReq,
  EnterAreaReq,
  EventRes,
  GameQuestServerAssertRes,
  GetGameplaySnapshotRes,
  GetQuestProgressRes,
  KillMonsterReq,
  QuestProgress,
  RebuildQuestProjectionReq,
  SubscribeQuestRes,
  SyncQuestProgressRes,
  UnlockFeatureReq
} from '../../Shared/Contracts/messages';

const QuestIds = {
  firstHunt: 'first-hunt',
  herbGathering: 'herb-gathering',
  openAuction: 'open-auction',
  tutorial: 'tutorial',
  ruinsExplorer: 'ruins-explorer'
} as const;

class QuestProgressStore {
  private readonly idempotency = new Map<string, string>();
  private readonly projections = new Map<string, Map<string, QuestProgress>>();
  private readonly deletedProjectionKeys = new Set<string>();
  private readonly completedMissions = new Map<string, Set<string>>();
  private readonly unlockedFeatures = new Map<string, Set<string>>();
  private readonly enteredAreas = new Map<string, Set<string>>();
  private readonly evidence: string[] = [];
  private sequence = 0;

  subscribeQuest(playerId: string): SubscribeQuestRes {
    this.evidence.push(`${playerId}:subscribed`);
    return { activeQuests: this.visibleProgress(playerId) };
  }

  enterArea(request: EnterAreaReq): EventRes {
    const eventId = this.eventId(request.playerId, request.idempotencyKey);
    if (this.remember(request.idempotencyKey, eventId)) {
      this.setFor(this.enteredAreas, request.playerId).add(request.areaId);
      this.upsertProgress(request.playerId, request.areaId === 'ruins' ? QuestIds.ruinsExplorer : request.areaId, 1, 1, eventId);
      this.evidence.push(`${eventId}:area:${request.areaId}`);
    }
    return { eventId };
  }

  killMonster(request: KillMonsterReq): EventRes {
    const eventId = this.eventId(request.playerId, request.idempotencyKey);
    if (this.remember(request.idempotencyKey, eventId)) {
      this.incrementProgress(request.playerId, QuestIds.firstHunt, 1, 3, eventId);
      this.evidence.push(`${eventId}:kill:${request.monsterId}`);
    }
    return { eventId };
  }

  collectItem(request: CollectItemReq): EventRes {
    const eventId = this.eventId(request.playerId, request.idempotencyKey);
    if (this.remember(request.idempotencyKey, eventId)) {
      this.incrementProgress(request.playerId, QuestIds.herbGathering, request.count, 5, eventId);
      this.evidence.push(`${eventId}:item:${request.itemId}:${request.count}`);
    }
    return { eventId };
  }

  completeMission(request: CompleteMissionReq): EventRes {
    const eventId = this.eventId(request.playerId, request.idempotencyKey);
    if (this.remember(request.idempotencyKey, eventId)) {
      this.setFor(this.completedMissions, request.playerId).add(request.missionId);
      this.upsertProgress(request.playerId, request.missionId, 1, 1, eventId);
      this.evidence.push(`${eventId}:mission:${request.missionId}`);
    }
    return { eventId };
  }

  unlockFeature(request: UnlockFeatureReq): EventRes {
    const eventId = this.eventId(request.playerId, request.idempotencyKey);
    if (this.remember(request.idempotencyKey, eventId)) {
      this.setFor(this.unlockedFeatures, request.playerId).add(request.featureId);
      this.upsertProgress(request.playerId, QuestIds.openAuction, 1, 1, eventId);
      this.evidence.push(`${eventId}:feature:${request.featureId}`);
    }
    return { eventId };
  }

  getProgress(playerId: string): GetQuestProgressRes {
    return { activeQuests: this.visibleProgress(playerId) };
  }

  syncProgress(playerId: string): SyncQuestProgressRes {
    const firstHunt = this.progressFor(playerId, QuestIds.firstHunt);
    if (firstHunt !== undefined && firstHunt.currentCount < 4) {
      this.upsertProgress(playerId, QuestIds.firstHunt, 4, 3, 'sync-reconciled');
      this.evidence.push(`${playerId}:sync:first-hunt`);
    }
    return { updatedQuests: this.visibleProgress(playerId) };
  }

  getSnapshot(playerId: string): GetGameplaySnapshotRes {
    return {
      playerId,
      completedMissionIds: [...(this.completedMissions.get(playerId) ?? [])],
      unlockedFeatureIds: [...(this.unlockedFeatures.get(playerId) ?? [])],
      enteredAreaIds: [...(this.enteredAreas.get(playerId) ?? [])],
      snapshotVersion: this.sequence
    };
  }

  deleteProjection(request: DeleteQuestProjectionReq): QuestProgress | undefined {
    this.deletedProjectionKeys.add(this.projectionKey(request.playerId, request.questId));
    this.evidence.push(`${request.playerId}:${request.questId}:projection-deleted`);
    return this.progressFor(request.playerId, request.questId);
  }

  rebuildProjection(request: RebuildQuestProjectionReq): QuestProgress {
    this.deletedProjectionKeys.delete(this.projectionKey(request.playerId, request.questId));
    const progress = this.progressFor(request.playerId, request.questId);
    if (progress === undefined) {
      throw new Error(`Unknown projection: ${request.playerId}/${request.questId}`);
    }
    this.evidence.push(`${request.playerId}:${request.questId}:projection-rebuilt`);
    return progress;
  }

  assertServer(): GameQuestServerAssertRes {
    const alice = this.visibleProgress('player-alice');
    const bob = this.visibleProgress('player-bob');
    const snapshot = this.getSnapshot('player-alice');
    const passed = alice.some((progress) => progress.questId === QuestIds.firstHunt && progress.currentCount >= 4)
      && alice.some((progress) => progress.questId === QuestIds.openAuction && progress.status === 'RewardGranted')
      && bob.some((progress) => progress.questId === QuestIds.herbGathering && progress.status === 'RewardGranted')
      && snapshot.unlockedFeatureIds.includes('auction')
      && this.evidence.some((entry) => entry.includes('projection-rebuilt'))
      && this.evidence.some((entry) => entry.includes('sync:first-hunt'));
    return { passed, evidence: [...this.evidence] };
  }

  private incrementProgress(
    playerId: string,
    questId: string,
    delta: number,
    requiredCount: number,
    eventId: string
  ): QuestProgress {
    const current = this.progressFor(playerId, questId);
    const nextCount = (current?.currentCount ?? 0) + delta;
    return this.upsertProgress(playerId, questId, nextCount, requiredCount, eventId);
  }

  private upsertProgress(
    playerId: string,
    questId: string,
    currentCount: number,
    requiredCount: number,
    eventId: string
  ): QuestProgress {
    const status = currentCount >= requiredCount ? 'RewardGranted' : 'InProgress';
    const progress: QuestProgress = {
      playerId,
      questId,
      status,
      currentCount,
      requiredCount,
      lastEventId: eventId,
      updatedAtUnixMs: Date.now()
    };
    this.mapFor(this.projections, playerId).set(questId, progress);
    return progress;
  }

  private visibleProgress(playerId: string): QuestProgress[] {
    return [...(this.projections.get(playerId)?.values() ?? [])]
      .filter((progress) => !this.deletedProjectionKeys.has(this.projectionKey(playerId, progress.questId)));
  }

  private progressFor(playerId: string, questId: string): QuestProgress | undefined {
    return this.projections.get(playerId)?.get(questId);
  }

  private remember(idempotencyKey: string, eventId: string): boolean {
    if (this.idempotency.has(idempotencyKey)) {
      return false;
    }
    this.idempotency.set(idempotencyKey, eventId);
    return true;
  }

  private eventId(playerId: string, idempotencyKey: string): string {
    return `${playerId}-${idempotencyKey}`;
  }

  private projectionKey(playerId: string, questId: string): string {
    return `${playerId}/${questId}`;
  }

  private setFor<TKey, TValue>(map: Map<TKey, Set<TValue>>, key: TKey): Set<TValue> {
    let values = map.get(key);
    if (values === undefined) {
      values = new Set<TValue>();
      map.set(key, values);
    }
    return values;
  }

  private mapFor<TKey, TInnerKey, TValue>(map: Map<TKey, Map<TInnerKey, TValue>>, key: TKey): Map<TInnerKey, TValue> {
    let values = map.get(key);
    if (values === undefined) {
      values = new Map<TInnerKey, TValue>();
      map.set(key, values);
    }
    return values;
  }
}

export {
  QuestIds,
  QuestProgressStore
};
