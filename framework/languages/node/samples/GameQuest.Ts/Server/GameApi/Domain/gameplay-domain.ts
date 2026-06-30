import {
  collectItemReq,
  completeMissionReq,
  enterAreaReq,
  killMonsterReq,
  unlockFeatureReq
} from '../../../Shared/Contracts/messages';
import type {
  CollectItemReq,
  CompleteMissionReq,
  EnterAreaReq,
  KillMonsterReq,
  UnlockFeatureReq
} from '../../../Shared/Contracts/messages';

function monsterKilled(request: KillMonsterReq): KillMonsterReq {
  return killMonsterReq(request.playerId, request.monsterId, request.areaId, request.idempotencyKey);
}

function itemCollected(request: CollectItemReq): CollectItemReq {
  return collectItemReq(request.playerId, request.itemId, request.count, request.idempotencyKey);
}

function missionCompleted(request: CompleteMissionReq): CompleteMissionReq {
  return completeMissionReq(request.playerId, request.missionId, request.idempotencyKey);
}

function areaEntered(request: EnterAreaReq): EnterAreaReq {
  return enterAreaReq(request.playerId, request.areaId, request.idempotencyKey);
}

function featureUnlocked(request: UnlockFeatureReq): UnlockFeatureReq {
  return unlockFeatureReq(request.playerId, request.featureId, request.idempotencyKey);
}

export {
  areaEntered,
  featureUnlocked,
  itemCollected,
  missionCompleted,
  monsterKilled
};
