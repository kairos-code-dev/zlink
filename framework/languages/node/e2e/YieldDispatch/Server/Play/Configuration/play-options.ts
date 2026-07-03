export interface PlayOptions {
  readonly rid: string;
  readonly httpUrl: string;
  readonly controlEndpoint: string;
  readonly spotRouteEndpoint: string;
  readonly spotRouterEndpoint: string;
  readonly spotPubEndpoint: string;
  readonly delayEndpoint: string;
  readonly redisEndpoint: string;
  readonly redisKeyPrefix: string;
  readonly evidenceFile?: string;
  readonly logDir: string;
}

export function parsePlayOptions(args: readonly string[]): PlayOptions {
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
    rid: required(values, 'rid'),
    httpUrl: required(values, 'http-url'),
    controlEndpoint: required(values, 'control-endpoint'),
    spotRouteEndpoint: required(values, 'spot-route-endpoint'),
    spotRouterEndpoint: required(values, 'spot-router-endpoint'),
    spotPubEndpoint: required(values, 'spot-pub-endpoint'),
    delayEndpoint: required(values, 'delay-endpoint'),
    redisEndpoint: required(values, 'redis-endpoint'),
    redisKeyPrefix: required(values, 'redis-key-prefix'),
    evidenceFile: values.get('evidence-file'),
    logDir: required(values, 'log-dir')
  };
}

function required(values: ReadonlyMap<string, string>, key: string): string {
  const value = values.get(key);
  if (value === undefined || value.length === 0) {
    throw new Error(`--${key} is required.`);
  }
  return value;
}
