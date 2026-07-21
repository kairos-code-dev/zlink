const SampleNames = {
  questMissionRouteChannel: 'gamequest.quest-owner.route',
  playerStreamNode: 'gamequest-stream',
  playerActorType: 'gamequest-player',
  playerQuestSpotMesh: 'gamequest.player-quest.spot',
  requestTimeout: 5000,
  clientTimeout: 20000
} as const;

function questMissionRouteRid(playerId: string): string {
  return questMissionInstanceRid(questMissionOwnerInstanceId(playerId));
}

function questMissionRouteChannel(playerId: string): string {
  return questMissionInstanceChannel(questMissionOwnerInstanceId(playerId));
}

function questMissionInstanceChannel(instanceId: 'mission-a' | 'mission-b'): string {
  return `${SampleNames.questMissionRouteChannel}.${instanceId}`;
}

function questMissionInstanceRid(instanceId: 'mission-a' | 'mission-b'): string {
  return instanceId === 'mission-a' ? 'gamequest-mission-a' : 'gamequest-mission-b';
}

function questMissionOwnerInstanceId(playerId: string): 'mission-a' | 'mission-b' {
  return ownerIndex(playerId) === 0 ? 'mission-a' : 'mission-b';
}

function questMissionSpotRid(playerId: string): string {
  return `player-quest-${playerId}`;
}

function ownerIndex(playerId: string): number {
  let sum = 0;
  for (const byte of new TextEncoder().encode(playerId)) {
    sum += byte;
  }
  return sum % 2;
}

export {
  SampleNames,
  questMissionRouteChannel,
  questMissionInstanceChannel,
  questMissionRouteRid,
  questMissionInstanceRid,
  questMissionOwnerInstanceId,
  questMissionSpotRid
};
