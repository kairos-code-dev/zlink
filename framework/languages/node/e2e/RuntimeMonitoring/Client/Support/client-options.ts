export interface ClientOptions {
  readonly triggerUrl: string;
  readonly serviceUrl: string;
  readonly serviceBUrl: string;
  readonly throwServiceUrl: string;
  readonly redisEndpoint: string;
  readonly redisKeyPrefix: string;
  readonly serviceBChannelEndpoint: string;
  readonly serviceBSpotRouterEndpoint: string;
  readonly serviceBSpotPubEndpoint: string;
  readonly serviceMain: string;
  readonly serviceBConfig: string;
  readonly logDir: string;
  readonly scenario: string;
}

export function parseClientOptions(args: readonly string[]): ClientOptions {
  const values = new Map<string, string>();
  for (let i = 0; i < args.length; i += 1) {
    const key = args[i];
    if (!key.startsWith('--')) {
      continue;
    }
    if (i + 1 >= args.length) {
      throw new Error(`Missing value for ${key}.`);
    }
    values.set(key.slice(2), args[++i]);
  }
  return {
    triggerUrl: required(values, 'trigger-url'),
    serviceUrl: required(values, 'service-url'),
    serviceBUrl: required(values, 'service-b-url'),
    throwServiceUrl: required(values, 'throw-service-url'),
    redisEndpoint: required(values, 'redis-endpoint'),
    redisKeyPrefix: required(values, 'redis-key-prefix'),
    serviceBChannelEndpoint: required(values, 'service-b-channel-endpoint'),
    serviceBSpotRouterEndpoint: required(values, 'service-b-spot-router-endpoint'),
    serviceBSpotPubEndpoint: required(values, 'service-b-spot-pub-endpoint'),
    serviceMain: required(values, 'service-main'),
    serviceBConfig: required(values, 'service-b-config'),
    logDir: required(values, 'log-dir'),
    scenario: values.get('scenario') ?? 'all'
  };
}

function required(values: ReadonlyMap<string, string>, key: string): string {
  const value = values.get(key);
  if (value === undefined || value.length === 0) {
    throw new Error(`--${key} is required.`);
  }
  return value;
}
