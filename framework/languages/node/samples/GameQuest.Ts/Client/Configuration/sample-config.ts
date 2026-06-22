type GameQuestClientConfig = {
  registryRouterEndpoint: string;
  questEndpoint: string;
};

function loadSampleConfig(): GameQuestClientConfig {
  return {
    registryRouterEndpoint: process.env.GAMEQUEST_REGISTRY_ROUTER_ENDPOINT ?? 'tcp://127.0.0.1:31084',
    questEndpoint: process.env.GAMEQUEST_QUEST_ENDPOINT ?? 'tcp://127.0.0.1:31092'
  };
}

export {
  loadSampleConfig
};

export type {
  GameQuestClientConfig
};
