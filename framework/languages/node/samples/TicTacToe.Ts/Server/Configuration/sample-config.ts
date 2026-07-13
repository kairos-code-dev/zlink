import * as fs from 'node:fs';
type TicTacToeSampleConfig = {
  instanceName: string;
  apiIndex: number;
  playIndex: number;
  apiHttpEndpoint: string;
  apiEndpoints: string[];
  apiHttpEndpoints: string[];
  playEndpoint: string;
  playChannelEndpoints: string[];
  playEndpoints: string[];
  playSpotEndpoint: string;
  playSpotEndpoints: string[];
  playSpotPubSubEndpoint: string;
  playSpotPubSubEndpoints: string[];
  playStreamEndpoint: string;
  redisEndpoint: string;
  redisKeyPrefix: string;
  playSpotNodeRid: string;
  peerPlaySpotNodeRid: string;
  peerPlaySpotEndpoint: string;
  peerPlaySpotPubEndpoint: string;
};

function loadSampleConfig(): TicTacToeSampleConfig {
  const configPath = process.env.ZLINK_SAMPLE_CONFIG;
  if (configPath !== undefined && configPath.length > 0) {
    return JSON.parse(fs.readFileSync(configPath, 'utf8')).sample;
  }
  return {
    instanceName: process.env.TICTACTOE_INSTANCE_NAME ?? 'sample',
    apiIndex: Number(process.env.TICTACTOE_API_INDEX ?? '0'),
    playIndex: Number(process.env.TICTACTOE_PLAY_INDEX ?? '0'),
    apiHttpEndpoint: requireEnv('TICTACTOE_API_HTTP_ENDPOINT'),
    apiEndpoints: readListEnv('TICTACTOE_API_ENDPOINTS', requireEnv('TICTACTOE_API_ENDPOINT')),
    apiHttpEndpoints: readListEnv('TICTACTOE_API_HTTP_ENDPOINTS', requireEnv('TICTACTOE_API_HTTP_ENDPOINT')),
    playEndpoint: requireEnv('TICTACTOE_PLAY_STREAM_ENDPOINT'),
    playChannelEndpoints: readListEnv('TICTACTOE_PLAY_CHANNEL_ENDPOINTS', requireEnv('TICTACTOE_PLAY_ENDPOINT')),
    playEndpoints: readListEnv('TICTACTOE_PLAY_STREAM_ENDPOINTS', requireEnv('TICTACTOE_PLAY_STREAM_ENDPOINT')),
    playSpotEndpoint: requireEnv('TICTACTOE_PLAY_SPOT_ENDPOINT'),
    playSpotEndpoints: readListEnv('TICTACTOE_PLAY_SPOT_ENDPOINTS', requireEnv('TICTACTOE_PLAY_SPOT_ENDPOINT')),
    playSpotPubSubEndpoint: requireEnv('TICTACTOE_PLAY_SPOT_PUBSUB_ENDPOINT'),
    playSpotPubSubEndpoints: readListEnv(
      'TICTACTOE_PLAY_SPOT_PUBSUB_ENDPOINTS',
      requireEnv('TICTACTOE_PLAY_SPOT_PUBSUB_ENDPOINT')
    ),
    playStreamEndpoint: requireEnv('TICTACTOE_PLAY_STREAM_ENDPOINT'),
    redisEndpoint: requireEnv('TICTACTOE_REDIS_ENDPOINT'),
    redisKeyPrefix: process.env.TICTACTOE_REDIS_KEY_PREFIX ?? 'tictactoe:sample:',
    playSpotNodeRid: process.env.TICTACTOE_PLAY_SPOT_NODE_RID ?? 'play-node-1',
    peerPlaySpotNodeRid: process.env.TICTACTOE_PEER_PLAY_SPOT_NODE_RID ?? 'play-node-2',
    peerPlaySpotEndpoint: requireEnv('TICTACTOE_PEER_PLAY_SPOT_ENDPOINT'),
    peerPlaySpotPubEndpoint: process.env.TICTACTOE_PEER_PLAY_SPOT_PUB_ENDPOINT
      ?? requireEnv('TICTACTOE_PEER_PLAY_SPOT_PUBSUB_ENDPOINT')
  };
}

function requireEnv(name: string): string {
  const value = process.env[name];
  if (value === undefined || value.length === 0) {
    throw new Error(`${name} is required.`);
  }
  return value;
}

function readListEnv(name: string, fallback: string): string[] {
  const value = process.env[name];
  if (value === undefined || value.length === 0) {
    return [fallback];
  }
  return value.split(',').map((item) => item.trim()).filter((item) => item.length > 0);
}

export { loadSampleConfig };
export type { TicTacToeSampleConfig };
