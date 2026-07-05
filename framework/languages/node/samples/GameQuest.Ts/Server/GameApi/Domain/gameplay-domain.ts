import type { GameplayEventEnvelope } from '../../../Shared/Contracts/messages';

const GameplayDomain = {
  monsterKilled(playerId: string, monsterId: string, areaId: string, idempotencyKey: string, sourceApi: string): GameplayEventEnvelope {
    return createEvent(playerId, idempotencyKey, 'monsterKilled', monsterId, 1, sourceApi);
  },
  itemCollected(playerId: string, itemId: string, count: number, idempotencyKey: string, sourceApi: string): GameplayEventEnvelope {
    return createEvent(playerId, idempotencyKey, 'itemCollected', itemId, count, sourceApi);
  },
  missionCompleted(playerId: string, missionId: string, idempotencyKey: string, sourceApi: string): GameplayEventEnvelope {
    return createEvent(playerId, idempotencyKey, 'missionCompleted', missionId, 1, sourceApi);
  },
  areaEntered(playerId: string, areaId: string, idempotencyKey: string, sourceApi: string): GameplayEventEnvelope {
    return createEvent(playerId, idempotencyKey, 'areaEntered', areaId, 1, sourceApi);
  },
  featureUnlocked(playerId: string, featureId: string, idempotencyKey: string, sourceApi: string): GameplayEventEnvelope {
    return createEvent(playerId, idempotencyKey, 'featureUnlocked', featureId, 1, sourceApi);
  }
};

function createEvent(
  playerId: string,
  idempotencyKey: string,
  eventType: string,
  value: string,
  count: number,
  sourceApi: string
): GameplayEventEnvelope {
  return {
    eventId: `${playerId}-${idempotencyKey}`,
    playerId,
    idempotencyKey,
    eventType,
    value,
    count,
    sourceApi,
    createdAtUnixMs: Date.now()
  };
}

export { GameplayDomain };
