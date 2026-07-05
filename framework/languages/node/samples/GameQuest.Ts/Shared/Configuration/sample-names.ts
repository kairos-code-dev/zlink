const SampleNames = {
  questMissionRouteChannel: 'gamequest.quest-owner.route',
  playerStreamNode: 'gamequest-stream',
  playerQuestSpotMesh: 'gamequest.player-quest.spot',
  requestTimeout: 5000,
  clientTimeout: 20000
} as const;

function questMissionRouteRid(playerId: string): string {
  return ownerIndex(playerId) === 0 ? 'gamequest-mission-a' : 'gamequest-mission-b';
}

function questMissionInstanceRid(instanceId: 'mission-a' | 'mission-b'): string {
  return instanceId === 'mission-a' ? 'gamequest-mission-a' : 'gamequest-mission-b';
}

function questMissionSpotRid(playerId: string): string {
  return `player-quest-${playerId}`;
}

function ownerIndex(playerId: string): number {
  let sum = 0;
  for (const byte of Buffer.from(playerId, 'utf8')) {
    sum += byte;
  }
  return sum % 2;
}

export {
  SampleNames,
  questMissionRouteRid,
  questMissionInstanceRid,
  questMissionSpotRid
};
