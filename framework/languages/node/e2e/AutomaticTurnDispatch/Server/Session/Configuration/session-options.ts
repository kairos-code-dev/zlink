export interface SessionOptions {
  readonly rid: string;
  readonly httpUrl: string;
  readonly controlRouterEndpoint: string;
  readonly playControlEndpoints: readonly string[];
  readonly spotRouteEndpoint: string;
  readonly spotRouterEndpoint: string;
  readonly spotRouterPeers: readonly { rid: string; endpoint: string }[];
  readonly playSpotRouteEndpoints: readonly string[];
  readonly streamEndpoint: string;
  readonly redisEndpoint: string;
  readonly redisKeyPrefix: string;
  readonly evidenceFile?: string;
  readonly logDir: string;
}

export function parseSessionOptions(args: readonly string[]): SessionOptions {
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
    controlRouterEndpoint: required(values, 'control-router-endpoint'),
    playControlEndpoints: requiredList(values, 'play-control-endpoint'),
    spotRouteEndpoint: required(values, 'spot-route-endpoint'),
    spotRouterEndpoint: required(values, 'spot-router-endpoint'),
    spotRouterPeers: optionalPeers(values, 'spot-router-peer'),
    playSpotRouteEndpoints: requiredList(values, 'play-spot-route-endpoint'),
    streamEndpoint: required(values, 'stream-endpoint'),
    redisEndpoint: required(values, 'redis-endpoint'),
    redisKeyPrefix: required(values, 'redis-key-prefix'),
    evidenceFile: values.get('evidence-file'),
    logDir: required(values, 'log-dir')
  };
}

function optionalPeers(values: ReadonlyMap<string, string>, key: string): readonly { rid: string; endpoint: string }[] {
  return (values.get(key) ?? '').split(',').map((value) => value.trim()).filter(Boolean).map((entry) => {
    const separator = entry.indexOf('@');
    if (separator <= 0 || separator === entry.length - 1) throw new Error(`--${key} requires rid@endpoint entries.`);
    return { rid: entry.slice(0, separator), endpoint: entry.slice(separator + 1) };
  });
}

function requiredList(values: ReadonlyMap<string, string>, key: string): readonly string[] {
  return required(values, key).split(',').map((value) => value.trim()).filter((value) => value.length > 0);
}

function required(values: ReadonlyMap<string, string>, key: string): string {
  const value = values.get(key);
  if (value === undefined || value.length === 0) {
    throw new Error(`--${key} is required.`);
  }
  return value;
}
