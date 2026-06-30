type QuestStatus = 'InProgress' | 'Completed' | 'RewardGranted';
type Packetized = {
  packetName(): string;
};

type QuestProgress = {
  playerId: string;
  questId: string;
  status: QuestStatus;
  currentCount: number;
  requiredCount: number;
  lastEventId?: string;
  updatedAtUnixMs: number;
};

type EnterAreaReq = Packetized & {
  playerId: string;
  areaId: string;
  idempotencyKey: string;
};

type EventRes = {
  eventId: string;
};

type KillMonsterReq = Packetized & {
  playerId: string;
  monsterId: string;
  areaId: string;
  idempotencyKey: string;
};

type CollectItemReq = Packetized & {
  playerId: string;
  itemId: string;
  count: number;
  idempotencyKey: string;
};

type CompleteMissionReq = Packetized & {
  playerId: string;
  missionId: string;
  idempotencyKey: string;
};

type UnlockFeatureReq = Packetized & {
  playerId: string;
  featureId: string;
  idempotencyKey: string;
};

type SubscribeQuestReq = Packetized & {
  playerId: string;
};

type SubscribeQuestRes = {
  activeQuests: QuestProgress[];
};

type GetQuestProgressReq = Packetized & {
  playerId: string;
};

type GetQuestProgressRes = {
  activeQuests: QuestProgress[];
};

type SyncQuestProgressReq = Packetized & {
  playerId: string;
};

type SyncQuestProgressRes = {
  updatedQuests: QuestProgress[];
};

type QuestProgressNotify = {
  playerId: string;
  progress: QuestProgress;
};

type QuestCompletedNotify = {
  playerId: string;
  progress: QuestProgress;
  rewardGranted: boolean;
};

type GetGameplaySnapshotReq = Packetized & {
  playerId: string;
};

type GetGameplaySnapshotRes = {
  playerId: string;
  completedMissionIds: string[];
  unlockedFeatureIds: string[];
  enteredAreaIds: string[];
  snapshotVersion: number;
};

type DeleteQuestProjectionReq = Packetized & {
  playerId: string;
  questId: string;
};

type RebuildQuestProjectionReq = Packetized & {
  playerId: string;
  questId: string;
};

type GameQuestServerAssertRes = {
  passed: boolean;
  evidence: string[];
};

const PacketNames = {
  enterAreaReq: 'EnterAreaReq',
  killMonsterReq: 'KillMonsterReq',
  collectItemReq: 'CollectItemReq',
  completeMissionReq: 'CompleteMissionReq',
  unlockFeatureReq: 'UnlockFeatureReq',
  subscribeQuestReq: 'SubscribeQuestReq',
  getQuestProgressReq: 'GetQuestProgressReq',
  syncQuestProgressReq: 'SyncQuestProgressReq',
  getGameplaySnapshotReq: 'GetGameplaySnapshotReq',
  deleteQuestProjectionReq: 'DeleteQuestProjectionReq',
  rebuildQuestProjectionReq: 'RebuildQuestProjectionReq',
  gameQuestServerAssertReq: 'GameQuestServerAssertReq',
  questProgressNotify: 'QuestProgressNotify',
  questCompletedNotify: 'QuestCompletedNotify'
} as const;

function enterAreaReq(playerId: string, areaId: string, idempotencyKey: string): EnterAreaReq {
  return { playerId, areaId, idempotencyKey, packetName: () => PacketNames.enterAreaReq };
}

function killMonsterReq(playerId: string, monsterId: string, areaId: string, idempotencyKey: string): KillMonsterReq {
  return { playerId, monsterId, areaId, idempotencyKey, packetName: () => PacketNames.killMonsterReq };
}

function collectItemReq(playerId: string, itemId: string, count: number, idempotencyKey: string): CollectItemReq {
  return { playerId, itemId, count, idempotencyKey, packetName: () => PacketNames.collectItemReq };
}

function completeMissionReq(playerId: string, missionId: string, idempotencyKey: string): CompleteMissionReq {
  return { playerId, missionId, idempotencyKey, packetName: () => PacketNames.completeMissionReq };
}

function unlockFeatureReq(playerId: string, featureId: string, idempotencyKey: string): UnlockFeatureReq {
  return { playerId, featureId, idempotencyKey, packetName: () => PacketNames.unlockFeatureReq };
}

function subscribeQuestReq(playerId: string): SubscribeQuestReq {
  return { playerId, packetName: () => PacketNames.subscribeQuestReq };
}

function getQuestProgressReq(playerId: string): GetQuestProgressReq {
  return { playerId, packetName: () => PacketNames.getQuestProgressReq };
}

function syncQuestProgressReq(playerId: string): SyncQuestProgressReq {
  return { playerId, packetName: () => PacketNames.syncQuestProgressReq };
}

function getGameplaySnapshotReq(playerId: string): GetGameplaySnapshotReq {
  return { playerId, packetName: () => PacketNames.getGameplaySnapshotReq };
}

function deleteQuestProjectionReq(playerId: string, questId: string): DeleteQuestProjectionReq {
  return { playerId, questId, packetName: () => PacketNames.deleteQuestProjectionReq };
}

function rebuildQuestProjectionReq(playerId: string, questId: string): RebuildQuestProjectionReq {
  return { playerId, questId, packetName: () => PacketNames.rebuildQuestProjectionReq };
}

function gameQuestServerAssertReq(): Packetized {
  return { packetName: () => PacketNames.gameQuestServerAssertReq };
}

export {
  PacketNames,
  collectItemReq,
  completeMissionReq,
  enterAreaReq,
  getGameplaySnapshotReq,
  getQuestProgressReq,
  killMonsterReq,
  gameQuestServerAssertReq,
  rebuildQuestProjectionReq,
  deleteQuestProjectionReq,
  subscribeQuestReq,
  syncQuestProgressReq,
  unlockFeatureReq
};

export type {
  CollectItemReq,
  CompleteMissionReq,
  DeleteQuestProjectionReq,
  EnterAreaReq,
  EventRes,
  GameQuestServerAssertRes,
  GetGameplaySnapshotReq,
  GetGameplaySnapshotRes,
  GetQuestProgressReq,
  GetQuestProgressRes,
  KillMonsterReq,
  QuestProgress,
  QuestCompletedNotify,
  QuestProgressNotify,
  QuestStatus,
  RebuildQuestProjectionReq,
  SubscribeQuestReq,
  SubscribeQuestRes,
  SyncQuestProgressReq,
  SyncQuestProgressRes,
  UnlockFeatureReq
};
