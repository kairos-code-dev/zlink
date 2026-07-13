import * as fs from 'node:fs';
type BingoSampleConfig = {
  sessionEndpoint: string;
  sessionRouteEndpoint: string;
  sessionSpotEndpoint: string;
  sessionSpotNodeRid: string;
  preferredPlayNodeRid: string;
  playEndpoint: string;
  playRouteEndpoint: string;
  playSpotEndpoint: string;
  playSpotPubSubEndpoint: string;
  playSpotNodeRid: string;
  apiEndpoint: string;
  redisEndpoint: string;
  redisKeyPrefix: string;
};

const BINGO_SAMPLE_CONFIG = Symbol.for('BINGO_SAMPLE_CONFIG');

function loadSampleConfig(): BingoSampleConfig {
  const configPath = process.env.ZLINK_SAMPLE_CONFIG;
  if (configPath !== undefined && configPath.length > 0) {
    return JSON.parse(fs.readFileSync(configPath, 'utf8')).sample;
  }
  return {
    sessionEndpoint: requireEnv('BINGO_SESSION_ENDPOINT'),
    sessionRouteEndpoint: requireEnv('BINGO_SESSION_ROUTE_ENDPOINT'),
    sessionSpotEndpoint: requireEnv('BINGO_SESSION_SPOT_ENDPOINT'),
    sessionSpotNodeRid: process.env.BINGO_SESSION_SPOT_NODE_RID ?? 'bingo-session-node',
    preferredPlayNodeRid: process.env.BINGO_PREFERRED_PLAY_NODE_RID ?? '',
    playEndpoint: requireEnv('BINGO_PLAY_ENDPOINT'),
    playRouteEndpoint: requireEnv('BINGO_PLAY_ROUTE_ENDPOINT'),
    playSpotEndpoint: requireEnv('BINGO_PLAY_SPOT_ENDPOINT'),
    playSpotPubSubEndpoint: requireEnv('BINGO_PLAY_SPOT_PUBSUB_ENDPOINT'),
    playSpotNodeRid: process.env.BINGO_PLAY_SPOT_NODE_RID ?? 'bingo-play-node',
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

export { BINGO_SAMPLE_CONFIG, loadSampleConfig };
export type { BingoSampleConfig };
