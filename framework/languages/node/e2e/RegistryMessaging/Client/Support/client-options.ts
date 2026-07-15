export interface ClientOptions {
  readonly providerAUrl: string;
  readonly providerBUrl: string;
  readonly workflowUrl: string;
  readonly directConsumerUrl: string;
  readonly singleConsumerUrl: string;
  readonly backpressureConsumerUrl: string;
  readonly locationConsumerUrl: string;
  readonly providerMain: string;
  readonly consumerMain: string;
  readonly redisEndpoint: string;
  readonly redisKeyPrefix: string;
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
    providerAUrl: required(values, 'provider-a-url'),
    providerBUrl: required(values, 'provider-b-url'),
    workflowUrl: required(values, 'workflow-url'),
    directConsumerUrl: required(values, 'direct-consumer-url'),
    singleConsumerUrl: required(values, 'single-consumer-url'),
    backpressureConsumerUrl: required(values, 'backpressure-consumer-url'),
    locationConsumerUrl: required(values, 'location-consumer-url'),
    providerMain: required(values, 'provider-main'),
    consumerMain: required(values, 'consumer-main'),
    redisEndpoint: required(values, 'redis-endpoint'),
    redisKeyPrefix: required(values, 'redis-key-prefix'),
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
