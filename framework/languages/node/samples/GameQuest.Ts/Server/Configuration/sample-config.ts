type GameQuestServerConfig = {
  apiAHttpUrl: string;
  apiBHttpUrl: string;
  apiAStreamEndpoint: string;
  apiBStreamEndpoint: string;
  apiARouteEndpoint: string;
  apiBRouteEndpoint: string;
  missionAEndpoint: string;
  missionBEndpoint: string;
  missionASpotRouterEndpoint: string;
  missionBSpotRouterEndpoint: string;
  missionASpotEndpoint: string;
  missionBSpotEndpoint: string;
  redisEndpoint: string;
  redisKeyPrefix: string;
  workDir: string;
};

function loadSampleConfig(): GameQuestServerConfig {
  return {
    apiAHttpUrl: process.env.GAMEQUEST_API_A_HTTP ?? 'http://127.0.0.1:31201',
    apiBHttpUrl: process.env.GAMEQUEST_API_B_HTTP ?? 'http://127.0.0.1:31202',
    apiAStreamEndpoint: process.env.GAMEQUEST_API_A_STREAM ?? 'tcp://127.0.0.1:31203',
    apiBStreamEndpoint: process.env.GAMEQUEST_API_B_STREAM ?? 'tcp://127.0.0.1:31204',
    apiARouteEndpoint: process.env.GAMEQUEST_API_A_ROUTE ?? 'tcp://127.0.0.1:31205',
    apiBRouteEndpoint: process.env.GAMEQUEST_API_B_ROUTE ?? 'tcp://127.0.0.1:31206',
    missionAEndpoint: process.env.GAMEQUEST_MISSION_A_ROUTE ?? 'tcp://127.0.0.1:31207',
    missionBEndpoint: process.env.GAMEQUEST_MISSION_B_ROUTE ?? 'tcp://127.0.0.1:31208',
    missionASpotRouterEndpoint: process.env.GAMEQUEST_MISSION_A_SPOT_ROUTER ?? 'tcp://127.0.0.1:31209',
    missionBSpotRouterEndpoint: process.env.GAMEQUEST_MISSION_B_SPOT_ROUTER ?? 'tcp://127.0.0.1:31210',
    missionASpotEndpoint: process.env.GAMEQUEST_MISSION_A_SPOT ?? 'tcp://127.0.0.1:31211',
    missionBSpotEndpoint: process.env.GAMEQUEST_MISSION_B_SPOT ?? 'tcp://127.0.0.1:31212',
    redisEndpoint: requireEnv('GAMEQUEST_REDIS_ENDPOINT'),
    redisKeyPrefix: process.env.GAMEQUEST_REDIS_KEY_PREFIX ?? 'gamequest:node:',
    workDir: process.env.GAMEQUEST_WORK_DIR ?? '.gamequest-work'
  };
}

function requireEnv(name: string): string {
  const value = process.env[name];
  if (value === undefined || value.length === 0) {
    throw new Error(`${name} is required.`);
  }
  return value;
}

export { loadSampleConfig };
export type { GameQuestServerConfig };
