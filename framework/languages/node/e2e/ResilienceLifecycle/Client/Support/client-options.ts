export interface ClientOptions {
  readonly peerLocationUrl: string;
  readonly providerAUrl: string;
  readonly providerBUrl: string;
  readonly consumerUrl: string;
  readonly consumerUrls: readonly string[];
  readonly soakDurationMs: number;
  readonly redisEndpoint: string;
  readonly redisKeyPrefix: string;
  readonly redisContainer: string;
  readonly providerAChannelEndpoint: string;
  readonly providerBChannelEndpoint: string;
  readonly providerBRemapUrl: string;
  readonly providerBRemapChannelEndpoint: string;
  readonly providerBGreenUrl: string;
  readonly providerBGreenChannelEndpoint: string;
  readonly providerMain: string;
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
  const soakDurationSeconds = Number(required(values, 'soak-duration-seconds'));
  if (!Number.isInteger(soakDurationSeconds) || soakDurationSeconds < 120) {
    throw new Error('--soak-duration-seconds must be an integer of at least 120.');
  }
  return {
    peerLocationUrl: required(values, 'peer-location-url'),
    providerAUrl: required(values, 'provider-a-url'),
    providerBUrl: required(values, 'provider-b-url'),
    consumerUrl: required(values, 'consumer-url'),
    consumerUrls: required(values, 'consumer-urls').split(',').filter((value) => value.length > 0),
    soakDurationMs: soakDurationSeconds * 1000,
    redisEndpoint: required(values, 'redis-endpoint'),
    redisKeyPrefix: required(values, 'redis-key-prefix'),
    redisContainer: required(values, 'redis-container'),
    providerAChannelEndpoint: required(values, 'provider-a-channel-endpoint'),
    providerBChannelEndpoint: required(values, 'provider-b-channel-endpoint'),
    providerBRemapUrl: required(values, 'provider-b-remap-url'),
    providerBRemapChannelEndpoint: required(values, 'provider-b-remap-channel-endpoint'),
    providerBGreenUrl: required(values, 'provider-b-green-url'),
    providerBGreenChannelEndpoint: required(values, 'provider-b-green-channel-endpoint'),
    providerMain: required(values, 'provider-main'),
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
