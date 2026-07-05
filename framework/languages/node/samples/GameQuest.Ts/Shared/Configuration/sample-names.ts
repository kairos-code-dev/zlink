const SampleNames = {
  questMissionRouteChannel: 'gamequest.quest-mission.route',
  playerStreamNode: 'gamequest.player.stream',
  playerQuestSpotMesh: 'gamequest.player-quest.spot',
  requestTimeout: 5000,
  clientTimeout: 20000
} as const;

function questMissionRouteRid(playerId: string): string {
  return ownerIndex(playerId) === 0 ? 'mission-a' : 'mission-b';
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
  questMissionSpotRid
};
