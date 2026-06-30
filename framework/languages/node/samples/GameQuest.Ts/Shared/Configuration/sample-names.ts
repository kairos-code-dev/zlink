const SampleNames = {
  questMissionRouteChannel: 'gamequest.quest.mission.route',
  playerStreamNode: 'gamequest.player.stream'
} as const;

const SampleTimings = {
  requestTimeout: 10000,
  httpTimeout: 30000,
  workflowTimeout: 30000
} as const;

export {
  SampleNames,
  SampleTimings
};
