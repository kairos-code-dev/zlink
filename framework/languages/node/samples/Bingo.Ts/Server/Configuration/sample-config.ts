const fs = require('node:fs');

type BingoSampleConfig = {
  registryEndpoint: string;
  sessionEndpoint: string;
  playEndpoint: string;
  notificationEndpoint: string;
  apiEndpoint: string;
};

function loadSampleConfig(): BingoSampleConfig {
  const configPath = process.env.ZLINK_SAMPLE_CONFIG;
  if (configPath !== undefined && configPath.length > 0) {
    return JSON.parse(fs.readFileSync(configPath, 'utf8')).sample;
  }
  return {
    registryEndpoint: requireEnv('BINGO_REGISTRY_ENDPOINT'),
    sessionEndpoint: requireEnv('BINGO_SESSION_ENDPOINT'),
    playEndpoint: requireEnv('BINGO_PLAY_ENDPOINT'),
    notificationEndpoint: requireEnv('BINGO_NOTIFICATION_ENDPOINT'),
    apiEndpoint: requireEnv('BINGO_API_ENDPOINT')
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
export type { BingoSampleConfig };
