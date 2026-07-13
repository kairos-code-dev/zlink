class KillMonsterReq {
  constructor(readonly playerId: string, readonly monsterId: string, readonly areaId: string, readonly idempotencyKey: string) {}
}
type KillMonsterRes = { eventId: string };
class CollectItemReq {
  constructor(readonly playerId: string, readonly itemId: string, readonly count: number, readonly idempotencyKey: string) {}
}
type CollectItemRes = { eventId: string };
class CompleteMissionReq {
  constructor(readonly playerId: string, readonly missionId: string, readonly idempotencyKey: string) {}
}
type CompleteMissionRes = { eventId: string };
class EnterAreaReq {
  constructor(readonly playerId: string, readonly areaId: string, readonly idempotencyKey: string) {}
}
type EnterAreaRes = { eventId: string };
class UnlockFeatureReq {
  constructor(readonly playerId: string, readonly featureId: string, readonly idempotencyKey: string) {}
}
type UnlockFeatureRes = { eventId: string };
class JoinSessionReq { constructor(readonly playerId: string) {} }
type JoinSessionRes = { activeQuests: QuestProgress[] };
class GetQuestProgressReq { constructor(readonly playerId: string) {} }
type GetQuestProgressRes = { activeQuests: QuestProgress[] };
class SyncQuestProgressReq { constructor(readonly playerId: string) {} }
type SyncQuestProgressRes = { updatedQuests: QuestProgress[] };
class DeleteQuestProjectionReq { constructor(readonly playerId: string, readonly questId: string) {} }
type DeleteQuestProjectionRes = { deleted: boolean };
class RebuildQuestProjectionReq {
  constructor(readonly playerId: string, readonly questId: string, readonly count: number) {}
}
type GetGameplaySnapshotReq = { playerId: string };
type GetGameplaySnapshotRes = {
  playerId: string;
  killCounts: KillCountSnapshot[];
  itemCounts: ItemCountSnapshot[];
  completedMissionIds: string[];
  unlockedFeatureIds: string[];
  enteredAreaIds: string[];
  snapshotVersion: number;
};
type KillCountSnapshot = { monsterId: string; areaId?: string; count: number };
type ItemCountSnapshot = { itemId: string; count: number };
class QuestProgressNotify {
  constructor(readonly playerId: string, readonly progress: QuestProgress, readonly targetConnectionId?: string) {}
}
class QuestCompletedNotify {
  readonly rewardGranted = true;
  constructor(readonly playerId: string, readonly progress: QuestProgress, readonly targetConnectionId?: string) {}
}
type QuestProgress = {
  playerId: string;
  questId: string;
  status: string;
  currentCount: number;
  requiredCount: number;
  lastEventId?: string;
  updatedAtUnixMs: number;
};
type GameplayEventEnvelope = {
  eventId: string;
  playerId: string;
  idempotencyKey: string;
  eventType: string;
  value: string;
  count: number;
  sourceApi: string;
  createdAtUnixMs: number;
};
class ApplyGameplayEventReq { constructor(readonly event: GameplayEventEnvelope) {} }
type ApplyGameplayEventRes = { applied: boolean; projection: QuestProgress[]; completedQuestId?: string };
type QuestProgressedEvent = {
  eventId: string;
  playerId: string;
  questId: string;
  delta: number;
  currentCount: number;
  requiredCount: number;
  sourceEventId: string;
};
type QuestCompletedEvent = {
  eventId: string;
  playerId: string;
  questId: string;
  sourceEventId: string;
  completedAtUnixMs: number;
};
type QuestRewardGrantedEvent = {
  eventId: string;
  playerId: string;
  questId: string;
  sourceEventId: string;
  rewardId: string;
  grantedAtUnixMs: number;
};
type QuestProgressReconciledEvent = {
  eventId: string;
  playerId: string;
  questId: string;
  currentCount: number;
  reason: string;
  reconciledAtUnixMs: number;
};
type StoredQuestEvent = {
  eventId: string;
  sourceEventId?: string;
  playerId: string;
  questId: string;
  eventType: string;
  payload: unknown;
  version: number;
  createdAtUnixMs: number;
};
type GameQuestServerAssertRes = { passed: boolean; evidence: string[] };

const QuestIds = {
  FirstHunt: 'first-hunt',
  OpenAuction: 'open-auction',
  HerbGathering: 'herb-gathering',
  TutorialPath: 'tutorial-path',
  RuinsExplorer: 'ruins-explorer'
} as const;

const QuestStatuses = {
  InProgress: 'InProgress',
  RewardGranted: 'RewardGranted'
} as const;

const PacketNames = {
  killMonsterReq: 'KillMonsterReq',
  killMonsterRes: 'KillMonsterRes',
  collectItemReq: 'CollectItemReq',
  collectItemRes: 'CollectItemRes',
  completeMissionReq: 'CompleteMissionReq',
  completeMissionRes: 'CompleteMissionRes',
  enterAreaReq: 'EnterAreaReq',
  enterAreaRes: 'EnterAreaRes',
  unlockFeatureReq: 'UnlockFeatureReq',
  unlockFeatureRes: 'UnlockFeatureRes',
  joinSessionReq: 'JoinSessionReq',
  joinSessionRes: 'JoinSessionRes',
  getQuestProgressReq: 'GetQuestProgressReq',
  getQuestProgressRes: 'GetQuestProgressRes',
  syncQuestProgressReq: 'SyncQuestProgressReq',
  syncQuestProgressRes: 'SyncQuestProgressRes',
  deleteQuestProjectionReq: 'DeleteQuestProjectionReq',
  deleteQuestProjectionRes: 'DeleteQuestProjectionRes',
  rebuildQuestProjectionReq: 'RebuildQuestProjectionReq',
  applyGameplayEventReq: 'ApplyGameplayEventReq',
  applyGameplayEventRes: 'ApplyGameplayEventRes',
  questProgressNotify: 'QuestProgressNotify',
  questCompletedNotify: 'QuestCompletedNotify'
} as const;

function killMonsterReq(playerId: string, monsterId: string, areaId: string, idempotencyKey: string): KillMonsterReq {
  return new KillMonsterReq(playerId, monsterId, areaId, idempotencyKey);
}

function collectItemReq(playerId: string, itemId: string, count: number, idempotencyKey: string): CollectItemReq {
  return new CollectItemReq(playerId, itemId, count, idempotencyKey);
}

function completeMissionReq(playerId: string, missionId: string, idempotencyKey: string): CompleteMissionReq {
  return new CompleteMissionReq(playerId, missionId, idempotencyKey);
}

function enterAreaReq(playerId: string, areaId: string, idempotencyKey: string): EnterAreaReq {
  return new EnterAreaReq(playerId, areaId, idempotencyKey);
}

function unlockFeatureReq(playerId: string, featureId: string, idempotencyKey: string): UnlockFeatureReq {
  return new UnlockFeatureReq(playerId, featureId, idempotencyKey);
}

function joinSessionReq(playerId: string): JoinSessionReq {
  return new JoinSessionReq(playerId);
}

function getQuestProgressReq(playerId: string): GetQuestProgressReq {
  return new GetQuestProgressReq(playerId);
}

function syncQuestProgressReq(playerId: string): SyncQuestProgressReq {
  return new SyncQuestProgressReq(playerId);
}

function deleteQuestProjectionReq(playerId: string, questId: string): DeleteQuestProjectionReq {
  return new DeleteQuestProjectionReq(playerId, questId);
}

function rebuildQuestProjectionReq(playerId: string, questId: string, count = 0): RebuildQuestProjectionReq {
  return new RebuildQuestProjectionReq(playerId, questId, count);
}

function applyGameplayEventReq(event: GameplayEventEnvelope): ApplyGameplayEventReq {
  return new ApplyGameplayEventReq(event);
}

export {
  QuestProgressNotify,
  QuestCompletedNotify,
  KillMonsterReq,
  CollectItemReq,
  CompleteMissionReq,
  EnterAreaReq,
  UnlockFeatureReq,
  JoinSessionReq,
  GetQuestProgressReq,
  SyncQuestProgressReq,
  DeleteQuestProjectionReq,
  RebuildQuestProjectionReq,
  ApplyGameplayEventReq,
  PacketNames,
  QuestIds,
  QuestStatuses,
  killMonsterReq,
  collectItemReq,
  completeMissionReq,
  enterAreaReq,
  unlockFeatureReq,
  joinSessionReq,
  getQuestProgressReq,
  syncQuestProgressReq,
  deleteQuestProjectionReq,
  rebuildQuestProjectionReq,
  applyGameplayEventReq
};

export type {
  KillMonsterRes,
  CollectItemRes,
  CompleteMissionRes,
  EnterAreaRes,
  UnlockFeatureRes,
  JoinSessionRes,
  GetQuestProgressRes,
  SyncQuestProgressRes,
  DeleteQuestProjectionRes,
  GetGameplaySnapshotReq,
  GetGameplaySnapshotRes,
  KillCountSnapshot,
  ItemCountSnapshot,
  QuestProgress,
  GameplayEventEnvelope,
  ApplyGameplayEventRes,
  QuestProgressedEvent,
  QuestCompletedEvent,
  QuestRewardGrantedEvent,
  QuestProgressReconciledEvent,
  StoredQuestEvent,
  GameQuestServerAssertRes
};
