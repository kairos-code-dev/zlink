type GameQuestClientConfig = {
  questEndpoint: string;
};

function loadSampleConfig(): GameQuestClientConfig {
  return {
    questEndpoint: process.env.GAMEQUEST_QUEST_ENDPOINT ?? 'tcp://127.0.0.1:31092'
  };
}

export {
  loadSampleConfig
};

export type {
  GameQuestClientConfig
};
