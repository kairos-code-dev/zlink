export interface SessionOptions {
  readonly rid: string;
  readonly httpUrl: string;
  readonly registryRouterEndpoint: string;
  readonly controlRouterEndpoint: string;
  readonly playControlEndpoints: readonly string[];
  readonly spotRouteEndpoint: string;
  readonly spotRouterEndpoint: string;
  readonly playSpotRouteEndpoints: readonly string[];
  readonly streamEndpoint: string;
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
    registryRouterEndpoint: required(values, 'registry-router-endpoint'),
    controlRouterEndpoint: required(values, 'control-router-endpoint'),
    playControlEndpoints: requiredList(values, 'play-control-endpoint'),
    spotRouteEndpoint: required(values, 'spot-route-endpoint'),
    spotRouterEndpoint: required(values, 'spot-router-endpoint'),
    playSpotRouteEndpoints: requiredList(values, 'play-spot-route-endpoint'),
    streamEndpoint: required(values, 'stream-endpoint'),
    evidenceFile: values.get('evidence-file'),
    logDir: required(values, 'log-dir')
  };
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
