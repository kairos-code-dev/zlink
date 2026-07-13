type SupportChatServerConfig = {
  apiChannelEndpoint: string;
  supportChannelEndpoint: string;
  supportSpotEndpoint: string;
  sessionSpotEndpoint: string;
  sessionStreamEndpoint: string;
  redisEndpoint: string;
  redisKeyPrefix: string;
};

function loadSampleConfig(): SupportChatServerConfig {
  return {
    apiChannelEndpoint: requireEnv('SUPPORTCHAT_API_CHANNEL_ENDPOINT'),
    supportChannelEndpoint: requireEnv('SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT'),
    supportSpotEndpoint: requireEnv('SUPPORTCHAT_SUPPORT_SPOT_ENDPOINT'),
    sessionSpotEndpoint: requireEnv('SUPPORTCHAT_SESSION_SPOT_ENDPOINT'),
    sessionStreamEndpoint: requireEnv('SUPPORTCHAT_STREAM_ENDPOINT'),
    redisEndpoint: requireEnv('SUPPORTCHAT_REDIS_ENDPOINT'),
    redisKeyPrefix: process.env.SUPPORTCHAT_REDIS_KEY_PREFIX ?? 'supportchat:node:'
  };
}

function requireEnv(name: string): string {
  const value = process.env[name];
  if (value === undefined || value.length === 0) throw new Error(`${name} is required.`);
  return value;
}

export { loadSampleConfig };
export type { SupportChatServerConfig };
