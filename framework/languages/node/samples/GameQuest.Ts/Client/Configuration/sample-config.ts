type GameQuestClientConfig = {
  apiAHttpUrl: string;
  apiBHttpUrl: string;
  apiAStreamEndpoint: string;
  apiBStreamEndpoint: string;
};

function loadSampleConfig(): GameQuestClientConfig {
  return {
    apiAHttpUrl: process.env.GAMEQUEST_API_A_HTTP ?? 'http://127.0.0.1:31085',
    apiBHttpUrl: process.env.GAMEQUEST_API_B_HTTP ?? 'http://127.0.0.1:31086',
    apiAStreamEndpoint: process.env.GAMEQUEST_API_A_STREAM ?? 'tcp://127.0.0.1:31087',
    apiBStreamEndpoint: process.env.GAMEQUEST_API_B_STREAM ?? 'tcp://127.0.0.1:31088'
  };
}

export {
  loadSampleConfig
};

export type {
  GameQuestClientConfig
};
