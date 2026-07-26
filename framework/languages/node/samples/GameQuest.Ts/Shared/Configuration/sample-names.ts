const SampleNames = {
  questMissionRouteChannel: 'gamequest.quest-owner.route',
  playerStreamNode: 'gamequest-stream',
  playerActorType: 'gamequest-player',
  playerQuestSpotMesh: 'gamequest.player-quest.spot',
  requestTimeout: 5000,
  clientTimeout: 20000
} as const;

function questMissionRouteChannel(_playerId: string): string {
  return SampleNames.questMissionRouteChannel;
}

function questMissionInstanceChannel(_instanceId: 'mission-a' | 'mission-b'): string {
  return SampleNames.questMissionRouteChannel;
}

function questMissionSpotRid(playerId: string): string {
  return `player-quest-${playerId}`;
}

export {
  SampleNames,
  questMissionRouteChannel,
  questMissionInstanceChannel,
  questMissionSpotRid
};
