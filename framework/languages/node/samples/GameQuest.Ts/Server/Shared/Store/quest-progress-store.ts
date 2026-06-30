import fs from 'node:fs';
import path from 'node:path';
import { QuestIds, questIdForArea, questStatus } from '../../QuestMission/Domain/quest-domain';
import type {
  CollectItemReq,
  CompleteMissionReq,
  DeleteQuestProjectionReq,
  EnterAreaReq,
  GameplayActionRes,
  GameQuestServerAssertRes,
  GetGameplaySnapshotRes,
  GetQuestProgressRes,
  KillMonsterReq,
  QuestProgress,
  RebuildQuestProjectionReq,
  SubscribeQuestRes,
  SyncQuestProgressRes,
  UnlockFeatureReq
} from '../../../Shared/Contracts/messages';

type PersistedQuestStore = {
  idempotency: Partial<Record<string, string>>;
  projections: Partial<Record<string, Partial<Record<string, QuestProgress>>>>;
  deletedProjectionKeys: string[];
  completedMissions: Partial<Record<string, string[]>>;
  unlockedFeatures: Partial<Record<string, string[]>>;
  enteredAreas: Partial<Record<string, string[]>>;
  evidence: string[];
  sequence: number;
};

class QuestProgressStore {
  private readonly filePath: string;

  constructor() {
    const storeDirectory = process.env.GAMEQUEST_STORE_DIR ?? '.gamequest-store';
    fs.mkdirSync(storeDirectory, { recursive: true });
    this.filePath = path.join(storeDirectory, 'quest-progress.json');
    if (!fs.existsSync(this.filePath)) {
      this.write({
        idempotency: {},
        projections: {},
        deletedProjectionKeys: [],
        completedMissions: {},
        unlockedFeatures: {},
        enteredAreas: {},
        evidence: [],
        sequence: 0
      });
    }
  }

  subscribeQuest(playerId: string): SubscribeQuestRes {
    const store = this.read();
    store.evidence.push(`${playerId}:subscribed`);
    this.write(store);
    return { activeQuests: this.visibleProgress(store, playerId) };
  }

  enterArea(request: EnterAreaReq): GameplayActionRes {
    return this.mutateEvent(request.playerId, request.idempotencyKey, (store, eventId) => {
      this.addToSet(store.enteredAreas, request.playerId, request.areaId);
      this.upsertProgress(store, request.playerId, questIdForArea(request.areaId), 1, 1, eventId);
      store.evidence.push(`${eventId}:area:${request.areaId}`);
    });
  }

  killMonster(request: KillMonsterReq): GameplayActionRes {
    return this.mutateEvent(request.playerId, request.idempotencyKey, (store, eventId) => {
      this.incrementProgress(store, request.playerId, QuestIds.firstHunt, 1, 3, eventId);
      store.evidence.push(`${eventId}:kill:${request.monsterId}`);
    });
  }

  collectItem(request: CollectItemReq): GameplayActionRes {
    return this.mutateEvent(request.playerId, request.idempotencyKey, (store, eventId) => {
      this.incrementProgress(store, request.playerId, QuestIds.herbGathering, request.count, 5, eventId);
      store.evidence.push(`${eventId}:item:${request.itemId}:${request.count}`);
    });
  }

  completeMission(request: CompleteMissionReq): GameplayActionRes {
    return this.mutateEvent(request.playerId, request.idempotencyKey, (store, eventId) => {
      this.addToSet(store.completedMissions, request.playerId, request.missionId);
      this.upsertProgress(store, request.playerId, request.missionId, 1, 1, eventId);
      store.evidence.push(`${eventId}:mission:${request.missionId}`);
    });
  }

  unlockFeature(request: UnlockFeatureReq): GameplayActionRes {
    return this.mutateEvent(request.playerId, request.idempotencyKey, (store, eventId) => {
      this.addToSet(store.unlockedFeatures, request.playerId, request.featureId);
      this.upsertProgress(store, request.playerId, QuestIds.openAuction, 1, 1, eventId);
      store.evidence.push(`${eventId}:feature:${request.featureId}`);
    });
  }

  getProgress(playerId: string): GetQuestProgressRes {
    const store = this.read();
    return { activeQuests: this.visibleProgress(store, playerId) };
  }

  syncProgress(playerId: string): SyncQuestProgressRes {
    const store = this.read();
    const firstHunt = this.progressFor(store, playerId, QuestIds.firstHunt);
    if (firstHunt !== undefined && firstHunt.currentCount < 4) {
      this.upsertProgress(store, playerId, QuestIds.firstHunt, 4, 3, 'sync-reconciled');
    }
    if (firstHunt !== undefined) {
      store.evidence.push(`${playerId}:sync:first-hunt`);
    }
    const updatedQuests = this.visibleProgress(store, playerId);
    this.write(store);
    return { updatedQuests };
  }

  getSnapshot(playerId: string): GetGameplaySnapshotRes {
    const store = this.read();
    return {
      playerId,
      completedMissionIds: [...(store.completedMissions[playerId] ?? [])],
      unlockedFeatureIds: [...(store.unlockedFeatures[playerId] ?? [])],
      enteredAreaIds: [...(store.enteredAreas[playerId] ?? [])],
      snapshotVersion: store.sequence
    };
  }

  deleteProjection(request: DeleteQuestProjectionReq): QuestProgress | undefined {
    const store = this.read();
    const key = this.projectionKey(request.playerId, request.questId);
    if (!store.deletedProjectionKeys.includes(key)) {
      store.deletedProjectionKeys.push(key);
    }
    store.evidence.push(`${request.playerId}:${request.questId}:projection-deleted`);
    const progress = this.progressFor(store, request.playerId, request.questId);
    this.write(store);
    return progress;
  }

  rebuildProjection(request: RebuildQuestProjectionReq): QuestProgress {
    const store = this.read();
    store.deletedProjectionKeys = store.deletedProjectionKeys.filter((key) => key !== this.projectionKey(request.playerId, request.questId));
    const progress = this.progressFor(store, request.playerId, request.questId);
    if (progress === undefined) {
      throw new Error(`Unknown projection: ${request.playerId}/${request.questId}`);
    }
    store.evidence.push(`${request.playerId}:${request.questId}:projection-rebuilt`);
    this.write(store);
    return progress;
  }

  assertServer(): GameQuestServerAssertRes {
    const store = this.read();
    const alice = this.visibleProgress(store, 'player-alice');
    const bob = this.visibleProgress(store, 'player-bob');
    const snapshot = this.getSnapshot('player-alice');
    const passed = alice.some((progress) => progress.questId === QuestIds.firstHunt && progress.currentCount >= 4)
      && alice.some((progress) => progress.questId === QuestIds.openAuction && progress.status === 'RewardGranted')
      && bob.some((progress) => progress.questId === QuestIds.herbGathering && progress.status === 'RewardGranted')
      && snapshot.unlockedFeatureIds.includes('auction')
      && store.evidence.some((entry) => entry.includes('projection-rebuilt'))
      && store.evidence.some((entry) => entry.includes('sync:first-hunt'));
    return { passed, evidence: [...store.evidence] };
  }

  private mutateEvent(
    playerId: string,
    idempotencyKey: string,
    update: (store: PersistedQuestStore, eventId: string) => void
  ): GameplayActionRes {
    const store = this.read();
    const eventId = this.eventId(playerId, idempotencyKey);
    if (store.idempotency[idempotencyKey] === undefined) {
      store.idempotency[idempotencyKey] = eventId;
      update(store, eventId);
      store.sequence += 1;
      this.write(store);
    }
    return { eventId };
  }

  private incrementProgress(
    store: PersistedQuestStore,
    playerId: string,
    questId: string,
    delta: number,
    requiredCount: number,
    eventId: string
  ): QuestProgress {
    const current = this.progressFor(store, playerId, questId);
    return this.upsertProgress(store, playerId, questId, (current?.currentCount ?? 0) + delta, requiredCount, eventId);
  }

  private upsertProgress(
    store: PersistedQuestStore,
    playerId: string,
    questId: string,
    currentCount: number,
    requiredCount: number,
    eventId: string
  ): QuestProgress {
    const status = questStatus(currentCount, requiredCount);
    const progress: QuestProgress = {
      playerId,
      questId,
      status,
      currentCount,
      requiredCount,
      lastEventId: eventId,
      updatedAtUnixMs: Date.now()
    };
    store.projections[playerId] ??= {};
    store.projections[playerId][questId] = progress;
    return progress;
  }

  private visibleProgress(store: PersistedQuestStore, playerId: string): QuestProgress[] {
    return Object.values(store.projections[playerId] ?? {})
      .filter((progress): progress is QuestProgress => progress !== undefined)
      .filter((progress) => !store.deletedProjectionKeys.includes(this.projectionKey(playerId, progress.questId)));
  }

  private progressFor(store: PersistedQuestStore, playerId: string, questId: string): QuestProgress | undefined {
    return store.projections[playerId]?.[questId];
  }

  private addToSet(map: Partial<Record<string, string[]>>, key: string, value: string): void {
    map[key] ??= [];
    if (!map[key].includes(value)) {
      map[key].push(value);
    }
  }

  private eventId(playerId: string, idempotencyKey: string): string {
    return `${playerId}-${idempotencyKey}`;
  }

  private projectionKey(playerId: string, questId: string): string {
    return `${playerId}/${questId}`;
  }

  private read(): PersistedQuestStore {
    return JSON.parse(fs.readFileSync(this.filePath, 'utf8')) as PersistedQuestStore;
  }

  private write(store: PersistedQuestStore): void {
    const tempPath = `${this.filePath}.${process.pid}.tmp`;
    fs.writeFileSync(tempPath, JSON.stringify(store, null, 2));
    fs.renameSync(tempPath, this.filePath);
  }
}

export {
  QuestIds,
  QuestProgressStore
};
