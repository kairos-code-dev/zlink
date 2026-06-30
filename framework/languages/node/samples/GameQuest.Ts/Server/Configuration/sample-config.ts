type GameQuestServerConfig = {
  registryPubEndpoint: string;
  registryRouterEndpoint: string;
  apiAHttpUrl: string;
  apiBHttpUrl: string;
  apiAStreamEndpoint: string;
  apiBStreamEndpoint: string;
  missionAEndpoint: string;
  missionBEndpoint: string;
  storeDirectory: string;
};

function loadSampleConfig(): GameQuestServerConfig {
  return {
    registryPubEndpoint: process.env.GAMEQUEST_REGISTRY_PUB_ENDPOINT ?? 'tcp://127.0.0.1:31083',
    registryRouterEndpoint: process.env.GAMEQUEST_REGISTRY_ROUTER_ENDPOINT ?? 'tcp://127.0.0.1:31084',
    apiAHttpUrl: process.env.GAMEQUEST_API_A_HTTP ?? 'http://127.0.0.1:31085',
    apiBHttpUrl: process.env.GAMEQUEST_API_B_HTTP ?? 'http://127.0.0.1:31086',
    apiAStreamEndpoint: process.env.GAMEQUEST_API_A_STREAM ?? 'tcp://127.0.0.1:31087',
    apiBStreamEndpoint: process.env.GAMEQUEST_API_B_STREAM ?? 'tcp://127.0.0.1:31088',
    missionAEndpoint: process.env.GAMEQUEST_MISSION_A_ENDPOINT ?? 'tcp://127.0.0.1:31092',
    missionBEndpoint: process.env.GAMEQUEST_MISSION_B_ENDPOINT ?? 'tcp://127.0.0.1:31093',
    storeDirectory: process.env.GAMEQUEST_STORE_DIR ?? '.gamequest-store'
  };
}

export {
  loadSampleConfig
};

export type {
  GameQuestServerConfig
};
