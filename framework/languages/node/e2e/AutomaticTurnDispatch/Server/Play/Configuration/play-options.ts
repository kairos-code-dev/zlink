export interface PlayOptions {
  readonly rid: string;
  readonly httpUrl: string;
  readonly controlEndpoint: string;
  readonly spotRouteEndpoint: string;
  readonly peerSpotRouteEndpoints: readonly string[];
  readonly spotRouterEndpoint: string;
  readonly spotPubEndpoint: string;
  readonly spotRouterPeers: readonly { rid: string; endpoint: string }[];
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
    peerSpotRouteEndpoints: optionalList(values, 'peer-spot-route-endpoint'),
    spotRouterEndpoint: required(values, 'spot-router-endpoint'),
    spotPubEndpoint: required(values, 'spot-pub-endpoint'),
    spotRouterPeers: optionalPeers(values, 'spot-router-peer'),
    delayEndpoint: required(values, 'delay-endpoint'),
    redisEndpoint: required(values, 'redis-endpoint'),
    redisKeyPrefix: required(values, 'redis-key-prefix'),
    evidenceFile: values.get('evidence-file'),
    logDir: required(values, 'log-dir')
  };
}

function optionalPeers(values: ReadonlyMap<string, string>, key: string): readonly { rid: string; endpoint: string }[] {
  return optionalList(values, key).map((entry) => {
    const separator = entry.indexOf('@');
    if (separator <= 0 || separator === entry.length - 1) throw new Error(`--${key} requires rid@endpoint entries.`);
    return { rid: entry.slice(0, separator), endpoint: entry.slice(separator + 1) };
  });
}

function optionalList(values: ReadonlyMap<string, string>, key: string): readonly string[] {
  return (values.get(key) ?? '').split(',').map((value) => value.trim()).filter((value) => value.length > 0);
}

function required(values: ReadonlyMap<string, string>, key: string): string {
  const value = values.get(key);
  if (value === undefined || value.length === 0) {
    throw new Error(`--${key} is required.`);
  }
  return value;
}
