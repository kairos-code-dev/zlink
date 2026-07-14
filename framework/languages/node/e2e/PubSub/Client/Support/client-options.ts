export interface ClientOptions {
  readonly publisherUrl: string;
  readonly lateSubscriberUrl: string;
  readonly publisherEndpoint: string;
  readonly redisEndpoint: string;
  readonly redisKeyPrefix: string;
  readonly publisherMain: string;
  readonly subscriberMain: string;
  readonly logDir: string;
  readonly scenario: string;
  readonly subscriberUrls: readonly string[];
}

export function parseClientOptions(args: readonly string[]): ClientOptions {
  const values = new Map<string, string[]>();
  for (let i = 0; i < args.length; i += 1) {
    const key = args[i];
    if (!key.startsWith('--') || i + 1 >= args.length) {
      throw new Error(`Invalid argument '${key}'.`);
    }
    const bucket = values.get(key.slice(2)) ?? [];
    bucket.push(args[++i]);
    values.set(key.slice(2), bucket);
  }

  return {
    publisherUrl: required(values, 'publisher-url'),
    lateSubscriberUrl: required(values, 'late-subscriber-url'),
    publisherEndpoint: required(values, 'publisher-endpoint'),
    redisEndpoint: required(values, 'redis-endpoint'),
    redisKeyPrefix: required(values, 'redis-key-prefix'),
    publisherMain: required(values, 'publisher-main'),
    subscriberMain: required(values, 'subscriber-main'),
    logDir: required(values, 'log-dir'),
    scenario: optional(values, 'scenario', 'all'),
    subscriberUrls: many(values, 'subscriber-url')
  };
}

function required(values: Map<string, string[]>, name: string): string {
  const bucket = values.get(name);
  if (bucket === undefined || bucket.length === 0) {
    throw new Error(`--${name} is required.`);
  }
  return bucket[bucket.length - 1];
}

function optional(values: Map<string, string[]>, name: string, fallback: string): string {
  const bucket = values.get(name);
  return bucket === undefined || bucket.length === 0 ? fallback : bucket[bucket.length - 1];
}

function many(values: Map<string, string[]>, name: string): readonly string[] {
  const bucket = values.get(name);
  if (bucket === undefined || bucket.length === 0) {
    throw new Error(`--${name} is required.`);
  }
  return bucket;
}
