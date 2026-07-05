import fs from 'node:fs';
import path from 'node:path';
import { QuestIds, QuestStatuses } from '../../../Shared/Contracts/messages';
import type {
  GameplayEventEnvelope,
  GetGameplaySnapshotRes,
  QuestProgress,
  QuestProgressReconciledEvent,
  StoredQuestEvent
} from '../../../Shared/Contracts/messages';

type GameQuestState = {
  gameplayEventsByKey: Record<string, GameplayEventEnvelope>;
  gameplayFacts: Record<string, PlayerFacts>;
  projections: QuestProgress[];
  questEvents: StoredQuestEvent[];
  ownerRehydrates: string[];
  evidence: string[];
};

type PlayerFacts = {
  kills: Record<string, number>;
  items: Record<string, number>;
  completedMissionIds: string[];
  unlockedFeatureIds: string[];
  enteredAreaIds: string[];
  snapshotVersion: number;
};

type ApplyResult = {
  projection: QuestProgress[];
  changedProgress?: QuestProgress;
  completedQuestId?: string;
};

const InitialState: GameQuestState = {
  gameplayEventsByKey: {},
  gameplayFacts: {},
  projections: [],
  questEvents: [],
  ownerRehydrates: [],
  evidence: []
};

class QuestProgressStore {
  private readonly stateFile: string;

  constructor(workDir = process.env.GAMEQUEST_WORK_DIR ?? '.gamequest-work') {
    this.stateFile = path.join(workDir, 'gamequest-state.json');
    fs.mkdirSync(path.dirname(this.stateFile), { recursive: true });
    if (!fs.existsSync(this.stateFile)) {
      this.writeState(InitialState);
    }
  }

  recordGameplayEvent(candidate: GameplayEventEnvelope): GameplayEventEnvelope {
    const state = this.readState();
    const key = `${candidate.playerId}:${candidate.idempotencyKey}`;
    if (Object.prototype.hasOwnProperty.call(state.gameplayEventsByKey, key)) {
      this.writeState(state);
      return state.gameplayEventsByKey[key];
    }

    state.gameplayEventsByKey[key] = candidate;
    this.applyFacts(state, candidate);
    this.addEvidence(state, `gameplay:${candidate.eventId}:${candidate.eventType}`);
    this.writeState(state);
    return candidate;
  }

  applyGameplayEvent(event: GameplayEventEnvelope, reason = 'owner-event'): ApplyResult {
    const state = this.readState();
    state.ownerRehydrates.push(`${event.playerId}:${event.eventId}`);
    const result = this.applyQuestRules(state, event, reason);
    this.writeState(state);
    return result;
  }

  syncProgress(playerId: string): QuestProgress[] {
    const state = this.readState();
    const facts = this.ensureFacts(state, playerId);
    const updated: QuestProgress[] = [];
    const firstHunt = facts.kills['wolf:forest'] ?? 0;
    if (firstHunt > 0) {
      updated.push(this.upsertProgress(state, playerId, QuestIds.FirstHunt, firstHunt, 3, `sync-${playerId}-first-hunt`));
      this.appendQuestEvent(state, {
        eventId: `reconcile-${playerId}-first-hunt-${Date.now()}`,
        playerId,
        questId: QuestIds.FirstHunt,
        eventType: 'QuestProgressReconciledEvent',
        sourceEventId: undefined,
        payload: {
          eventId: `reconcile-${playerId}-first-hunt`,
          playerId,
          questId: QuestIds.FirstHunt,
          currentCount: firstHunt,
          reason: 'sync',
          reconciledAtUnixMs: Date.now()
        } satisfies QuestProgressReconciledEvent
      });
    }
    this.addEvidence(state, `sync:${playerId}`);
    this.writeState(state);
    return this.readProjectionFromState(state, playerId);
  }

  readProjection(playerId: string): QuestProgress[] {
    return this.readProjectionFromState(this.readState(), playerId);
  }

  mergeProjection(playerId: string, projection: QuestProgress[]): void {
    const state = this.readState();
    state.projections = state.projections.filter((progress) => progress.playerId !== playerId);
    state.projections.push(...projection);
    this.writeState(state);
  }

  deleteProjection(playerId: string, questId: string): void {
    const state = this.readState();
    state.projections = state.projections.filter((progress) =>
      !(progress.playerId === playerId && progress.questId === questId));
    this.addEvidence(state, `projection-delete:${playerId}:${questId}`);
    this.writeState(state);
  }

  rebuildProjection(playerId: string, questId: string): QuestProgress {
    const state = this.readState();
    const matching = state.questEvents.filter((event) => event.playerId === playerId && event.questId === questId);
    if (matching.length === 0) {
      throw new Error(`Quest stream was not found for ${playerId}/${questId}.`);
    }
    const currentCount = matching.reduce((count, event) => {
      const payload = event.payload as { currentCount?: number };
      return Math.max(count, payload.currentCount ?? count);
    }, 0);
    const requiredCount = questId === QuestIds.HerbGathering ? 5 : 3;
    const rebuilt = this.upsertProgress(
      state,
      playerId,
      questId,
      currentCount,
      requiredCount,
      matching[matching.length - 1].sourceEventId
    );
    this.addEvidence(state, `projection-rebuild:${playerId}:${questId}`);
    this.writeState(state);
    return rebuilt;
  }

  readGameplaySnapshot(playerId: string): GetGameplaySnapshotRes {
    const facts = this.ensureFacts(this.readState(), playerId);
    return {
      playerId,
      killCounts: Object.entries(facts.kills).map(([key, count]) => {
        const [monsterId, areaId] = key.split(':');
        return { monsterId, areaId, count };
      }),
      itemCounts: Object.entries(facts.items).map(([itemId, count]) => ({ itemId, count })),
      completedMissionIds: [...facts.completedMissionIds],
      unlockedFeatureIds: [...facts.unlockedFeatureIds],
      enteredAreaIds: [...facts.enteredAreaIds],
      snapshotVersion: facts.snapshotVersion
    };
  }

  killWithoutPublish(playerId: string): void {
    const state = this.readState();
    const facts = this.ensureFacts(state, playerId);
    facts.kills['wolf:forest'] = (facts.kills['wolf:forest'] ?? 0) + 1;
    facts.snapshotVersion += 1;
    this.addEvidence(state, `missed-publish:${playerId}`);
    this.writeState(state);
  }

  closeOwner(playerId: string, owner: string): void {
    const state = this.readState();
    state.ownerRehydrates.push(`${owner}:close:${playerId}`);
    this.addEvidence(state, `owner-close:${owner}:${playerId}`);
    this.writeState(state);
  }

  readQuestEventNames(): string[] {
    return this.readState().questEvents.map((event) => event.eventType);
  }

  assertServerEvidence(): { passed: boolean; evidence: string[] } {
    const state = this.readState();
    const evidence = [...state.evidence];
    const passed = [
      'completed:player-alice:first-hunt',
      'completed:player-alice:open-auction',
      'completed:player-bob:herb-gathering',
      'projection-rebuild:player-bob:herb-gathering',
      'missed-publish:player-alice',
      'sync:player-alice'
    ].every((entry) => evidence.includes(entry));
    return { passed, evidence };
  }

  private applyFacts(state: GameQuestState, event: GameplayEventEnvelope): void {
    const facts = this.ensureFacts(state, event.playerId);
    if (event.eventType === 'monsterKilled') {
      const key = `${event.value}:forest`;
      facts.kills[key] = (facts.kills[key] ?? 0) + event.count;
    } else if (event.eventType === 'itemCollected') {
      facts.items[event.value] = (facts.items[event.value] ?? 0) + event.count;
    } else if (event.eventType === 'missionCompleted') {
      addUnique(facts.completedMissionIds, event.value);
    } else if (event.eventType === 'areaEntered') {
      addUnique(facts.enteredAreaIds, event.value);
    } else if (event.eventType === 'featureUnlocked') {
      addUnique(facts.unlockedFeatureIds, event.value);
    }
    facts.snapshotVersion += 1;
  }

  private applyQuestRules(state: GameQuestState, event: GameplayEventEnvelope, reason: string): ApplyResult {
    let changedProgress: QuestProgress | undefined;
    let completedQuestId: string | undefined;
    if (event.eventType === 'monsterKilled' && event.value === 'wolf') {
      changedProgress = this.upsertProgress(state, event.playerId, QuestIds.FirstHunt, this.factKillCount(state, event.playerId), 3, event.eventId);
    } else if (event.eventType === 'featureUnlocked' && event.value === 'auction') {
      changedProgress = this.upsertProgress(state, event.playerId, QuestIds.OpenAuction, 1, 1, event.eventId);
    } else if (event.eventType === 'itemCollected' && event.value === 'healing-herb') {
      changedProgress = this.upsertProgress(state, event.playerId, QuestIds.HerbGathering, this.factItemCount(state, event.playerId), 5, event.eventId);
    } else if (event.eventType === 'missionCompleted' && event.value === 'tutorial') {
      changedProgress = this.upsertProgress(state, event.playerId, QuestIds.TutorialPath, 1, 1, event.eventId);
    } else if (event.eventType === 'areaEntered' && event.value === 'ruins') {
      changedProgress = this.upsertProgress(state, event.playerId, QuestIds.RuinsExplorer, 1, 1, event.eventId);
    }

    if (changedProgress !== undefined) {
      this.appendQuestEvent(state, {
        eventId: `${changedProgress.questId}-${event.eventId}-progress`,
        sourceEventId: event.eventId,
        playerId: event.playerId,
        questId: changedProgress.questId,
        eventType: reason === 'sync' ? 'QuestProgressReconciledEvent' : 'QuestProgressedEvent',
        payload: { ...changedProgress },
        version: 0,
        createdAtUnixMs: Date.now()
      });
      this.addEvidence(state, `progress:${event.playerId}:${changedProgress.questId}:${changedProgress.currentCount}`);
      if (changedProgress.status === QuestStatuses.RewardGranted) {
        completedQuestId = changedProgress.questId;
        this.appendQuestEvent(state, {
          eventId: `${changedProgress.questId}-${event.eventId}-completed`,
          sourceEventId: event.eventId,
          playerId: event.playerId,
          questId: changedProgress.questId,
          eventType: 'QuestCompletedEvent',
          payload: { eventId: `${changedProgress.questId}-completed`, playerId: event.playerId, questId: changedProgress.questId, sourceEventId: event.eventId, completedAtUnixMs: Date.now() },
          version: 0,
          createdAtUnixMs: Date.now()
        });
        this.appendQuestEvent(state, {
          eventId: `${changedProgress.questId}-${event.eventId}-reward`,
          sourceEventId: event.eventId,
          playerId: event.playerId,
          questId: changedProgress.questId,
          eventType: 'QuestRewardGrantedEvent',
          payload: { eventId: `${changedProgress.questId}-reward`, playerId: event.playerId, questId: changedProgress.questId, sourceEventId: event.eventId, rewardId: `reward-${changedProgress.questId}`, grantedAtUnixMs: Date.now() },
          version: 0,
          createdAtUnixMs: Date.now()
        });
        this.addEvidence(state, `completed:${event.playerId}:${changedProgress.questId}`);
      }
    }
    return {
      projection: this.readProjectionFromState(state, event.playerId),
      changedProgress,
      completedQuestId
    };
  }

  private upsertProgress(
    state: GameQuestState,
    playerId: string,
    questId: string,
    currentCount: number,
    requiredCount: number,
    lastEventId?: string
  ): QuestProgress {
    const progress: QuestProgress = {
      playerId,
      questId,
      status: currentCount >= requiredCount ? QuestStatuses.RewardGranted : QuestStatuses.InProgress,
      currentCount,
      requiredCount,
      lastEventId,
      updatedAtUnixMs: Date.now()
    };
    state.projections = state.projections.filter((candidate) =>
      !(candidate.playerId === playerId && candidate.questId === questId));
    state.projections.push(progress);
    return progress;
  }

  private factKillCount(state: GameQuestState, playerId: string): number {
    return this.ensureFacts(state, playerId).kills['wolf:forest'] ?? 0;
  }

  private factItemCount(state: GameQuestState, playerId: string): number {
    return this.ensureFacts(state, playerId).items['healing-herb'] ?? 0;
  }

  private appendQuestEvent(state: GameQuestState, event: Omit<StoredQuestEvent, 'version' | 'createdAtUnixMs'> & Partial<StoredQuestEvent>): void {
    state.questEvents.push({
      ...event,
      version: state.questEvents.length + 1,
      createdAtUnixMs: event.createdAtUnixMs ?? Date.now()
    });
  }

  private ensureFacts(state: GameQuestState, playerId: string): PlayerFacts {
    state.gameplayFacts[playerId] ??= {
      kills: {},
      items: {},
      completedMissionIds: [],
      unlockedFeatureIds: [],
      enteredAreaIds: [],
      snapshotVersion: 0
    };
    return state.gameplayFacts[playerId];
  }

  private readProjectionFromState(state: GameQuestState, playerId: string): QuestProgress[] {
    return state.projections.filter((progress) => progress.playerId === playerId);
  }

  private addEvidence(state: GameQuestState, entry: string): void {
    if (!state.evidence.includes(entry)) {
      state.evidence.push(entry);
    }
  }

  private readState(): GameQuestState {
    if (!fs.existsSync(this.stateFile)) {
      return structuredClone(InitialState);
    }
    return JSON.parse(fs.readFileSync(this.stateFile, 'utf8')) as GameQuestState;
  }

  private writeState(state: GameQuestState): void {
    fs.writeFileSync(this.stateFile, JSON.stringify(state, null, 2));
  }
}

function addUnique(values: string[], value: string): void {
  if (!values.includes(value)) {
    values.push(value);
  }
}

export { QuestProgressStore };
