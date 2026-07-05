type PacketNamed = { packetName(): string };

type KillMonsterReq = PacketNamed & { playerId: string; monsterId: string; areaId: string; idempotencyKey: string };
type KillMonsterRes = { eventId: string };
type CollectItemReq = PacketNamed & { playerId: string; itemId: string; count: number; idempotencyKey: string };
type CollectItemRes = { eventId: string };
type CompleteMissionReq = PacketNamed & { playerId: string; missionId: string; idempotencyKey: string };
type CompleteMissionRes = { eventId: string };
type EnterAreaReq = PacketNamed & { playerId: string; areaId: string; idempotencyKey: string };
type EnterAreaRes = { eventId: string };
type UnlockFeatureReq = PacketNamed & { playerId: string; featureId: string; idempotencyKey: string };
type UnlockFeatureRes = { eventId: string };
type JoinSessionReq = PacketNamed & { playerId: string };
type JoinSessionRes = { activeQuests: QuestProgress[] };
type GetQuestProgressReq = PacketNamed & { playerId: string };
type GetQuestProgressRes = { activeQuests: QuestProgress[] };
type SyncQuestProgressReq = PacketNamed & { playerId: string };
type SyncQuestProgressRes = { updatedQuests: QuestProgress[] };
type DeleteQuestProjectionReq = PacketNamed & { playerId: string; questId: string };
type DeleteQuestProjectionRes = { deleted: boolean };
type RebuildQuestProjectionReq = PacketNamed & { playerId: string; questId: string; count: number };
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
type QuestProgressNotify = { playerId: string; targetConnectionId?: string; progress: QuestProgress };
type QuestCompletedNotify = { playerId: string; targetConnectionId?: string; progress: QuestProgress; rewardGranted: boolean };
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
type ApplyGameplayEventReq = PacketNamed & { event: GameplayEventEnvelope };
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
  return { playerId, monsterId, areaId, idempotencyKey, packetName: () => PacketNames.killMonsterReq };
}

function collectItemReq(playerId: string, itemId: string, count: number, idempotencyKey: string): CollectItemReq {
  return { playerId, itemId, count, idempotencyKey, packetName: () => PacketNames.collectItemReq };
}

function completeMissionReq(playerId: string, missionId: string, idempotencyKey: string): CompleteMissionReq {
  return { playerId, missionId, idempotencyKey, packetName: () => PacketNames.completeMissionReq };
}

function enterAreaReq(playerId: string, areaId: string, idempotencyKey: string): EnterAreaReq {
  return { playerId, areaId, idempotencyKey, packetName: () => PacketNames.enterAreaReq };
}

function unlockFeatureReq(playerId: string, featureId: string, idempotencyKey: string): UnlockFeatureReq {
  return { playerId, featureId, idempotencyKey, packetName: () => PacketNames.unlockFeatureReq };
}

function joinSessionReq(playerId: string): JoinSessionReq {
  return { playerId, packetName: () => PacketNames.joinSessionReq };
}

function getQuestProgressReq(playerId: string): GetQuestProgressReq {
  return { playerId, packetName: () => PacketNames.getQuestProgressReq };
}

function syncQuestProgressReq(playerId: string): SyncQuestProgressReq {
  return { playerId, packetName: () => PacketNames.syncQuestProgressReq };
}

function deleteQuestProjectionReq(playerId: string, questId: string): DeleteQuestProjectionReq {
  return { playerId, questId, packetName: () => PacketNames.deleteQuestProjectionReq };
}

function rebuildQuestProjectionReq(playerId: string, questId: string, count = 0): RebuildQuestProjectionReq {
  return { playerId, questId, count, packetName: () => PacketNames.rebuildQuestProjectionReq };
}

function applyGameplayEventReq(event: GameplayEventEnvelope): ApplyGameplayEventReq {
  return { event, packetName: () => PacketNames.applyGameplayEventReq };
}

export {
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
  KillMonsterReq,
  KillMonsterRes,
  CollectItemReq,
  CollectItemRes,
  CompleteMissionReq,
  CompleteMissionRes,
  EnterAreaReq,
  EnterAreaRes,
  UnlockFeatureReq,
  UnlockFeatureRes,
  JoinSessionReq,
  JoinSessionRes,
  GetQuestProgressReq,
  GetQuestProgressRes,
  SyncQuestProgressReq,
  SyncQuestProgressRes,
  DeleteQuestProjectionReq,
  DeleteQuestProjectionRes,
  RebuildQuestProjectionReq,
  GetGameplaySnapshotReq,
  GetGameplaySnapshotRes,
  KillCountSnapshot,
  ItemCountSnapshot,
  QuestProgressNotify,
  QuestCompletedNotify,
  QuestProgress,
  GameplayEventEnvelope,
  ApplyGameplayEventReq,
  ApplyGameplayEventRes,
  QuestProgressedEvent,
  QuestCompletedEvent,
  QuestRewardGrantedEvent,
  QuestProgressReconciledEvent,
  StoredQuestEvent,
  GameQuestServerAssertRes
};
