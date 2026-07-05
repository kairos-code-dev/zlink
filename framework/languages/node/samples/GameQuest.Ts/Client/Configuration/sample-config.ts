type GameQuestClientConfig = {
  apiAHttpUrl: string;
  apiBHttpUrl: string;
  apiAStreamEndpoint: string;
  apiBStreamEndpoint: string;
  missionAHttpUrl: string;
  missionBHttpUrl: string;
};

function loadSampleConfig(): GameQuestClientConfig {
  return {
    apiAHttpUrl: process.env.GAMEQUEST_API_A_HTTP ?? 'http://127.0.0.1:31201',
    apiBHttpUrl: process.env.GAMEQUEST_API_B_HTTP ?? 'http://127.0.0.1:31202',
    apiAStreamEndpoint: process.env.GAMEQUEST_API_A_STREAM ?? 'tcp://127.0.0.1:31203',
    apiBStreamEndpoint: process.env.GAMEQUEST_API_B_STREAM ?? 'tcp://127.0.0.1:31204',
    missionAHttpUrl: process.env.GAMEQUEST_MISSION_A_HTTP ?? 'http://127.0.0.1:31213',
    missionBHttpUrl: process.env.GAMEQUEST_MISSION_B_HTTP ?? 'http://127.0.0.1:31214'
  };
}

export { loadSampleConfig };
export type { GameQuestClientConfig };
