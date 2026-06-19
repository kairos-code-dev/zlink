import * as fs from 'node:fs';
type BingoSampleConfig = {
  registryPubEndpoint: string;
  registryRouterEndpoint: string;
  sessionEndpoint: string;
  playEndpoint: string;
  notificationEndpoint: string;
  apiEndpoint: string;
  redisEndpoint: string;
  redisKeyPrefix: string;
};

function loadSampleConfig(): BingoSampleConfig {
  const configPath = process.env.ZLINK_SAMPLE_CONFIG;
  if (configPath !== undefined && configPath.length > 0) {
    return JSON.parse(fs.readFileSync(configPath, 'utf8')).sample;
  }
  return {
    registryPubEndpoint: requireEnv('BINGO_REGISTRY_PUB_ENDPOINT'),
    registryRouterEndpoint: requireEnv('BINGO_REGISTRY_ROUTER_ENDPOINT'),
    sessionEndpoint: requireEnv('BINGO_SESSION_ENDPOINT'),
    playEndpoint: requireEnv('BINGO_PLAY_ENDPOINT'),
    notificationEndpoint: requireEnv('BINGO_NOTIFICATION_ENDPOINT'),
    apiEndpoint: requireEnv('BINGO_API_ENDPOINT'),
    redisEndpoint: requireEnv('BINGO_REDIS_ENDPOINT'),
    redisKeyPrefix: process.env.BINGO_REDIS_KEY_PREFIX ?? 'bingo:node:'
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
