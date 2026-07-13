interface ShoppingMallServerConfig {
  readonly apiAHttpUrl: string;
  readonly apiBHttpUrl: string;
  readonly workflowAHttpUrl: string;
  readonly workflowBHttpUrl: string;
  readonly workflowAChannelEndpoint: string;
  readonly workflowBChannelEndpoint: string;
  readonly workflowASpotEndpoint: string;
  readonly workflowBSpotEndpoint: string;
  readonly workflowASpotPubEndpoint: string;
  readonly workflowBSpotPubEndpoint: string;
  readonly redisEndpoint: string;
  readonly redisKeyPrefix: string;
}

function loadSampleConfig(): ShoppingMallServerConfig {
  return {
    apiAHttpUrl: requireEnv('SHOPPINGMALL_API_A_HTTP'),
    apiBHttpUrl: requireEnv('SHOPPINGMALL_API_B_HTTP'),
    workflowAHttpUrl: requireEnv('SHOPPINGMALL_WORKFLOW_A_HTTP'),
    workflowBHttpUrl: requireEnv('SHOPPINGMALL_WORKFLOW_B_HTTP'),
    workflowAChannelEndpoint: requireEnv('SHOPPINGMALL_WORKFLOW_A_CHANNEL_ENDPOINT'),
    workflowBChannelEndpoint: requireEnv('SHOPPINGMALL_WORKFLOW_B_CHANNEL_ENDPOINT'),
    workflowASpotEndpoint: requireEnv('SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT'),
    workflowBSpotEndpoint: requireEnv('SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT'),
    workflowASpotPubEndpoint: requireEnv('SHOPPINGMALL_WORKFLOW_A_SPOT_PUB_ENDPOINT'),
    workflowBSpotPubEndpoint: requireEnv('SHOPPINGMALL_WORKFLOW_B_SPOT_PUB_ENDPOINT'),
    redisEndpoint: requireEnv('SHOPPINGMALL_REDIS_ENDPOINT'),
    redisKeyPrefix: process.env.SHOPPINGMALL_REDIS_KEY_PREFIX ?? 'shoppingmall:node:'
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
export type { ShoppingMallServerConfig };
