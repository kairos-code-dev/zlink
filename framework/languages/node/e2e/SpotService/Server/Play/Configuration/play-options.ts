export interface PlayOptions {
  readonly rid: string;
  readonly httpUrl: string;
  readonly registryRouterEndpoint: string;
  readonly controlRouterEndpoint: string;
  readonly externalSpotEndpoint: string;
  readonly spotRouterEndpoint: string;
  readonly spotPubEndpoint: string;
  readonly clientSpotPubEndpoint?: string;
  readonly playAExternalSpotEndpoint?: string;
  readonly externalClientEndpoint?: string;
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
  const rid = required(values, 'rid');
  process.env.ZLINK_E2E_RID = rid;
  return {
    rid,
    httpUrl: required(values, 'http-url'),
    registryRouterEndpoint: required(values, 'registry-router-endpoint'),
    controlRouterEndpoint: required(values, 'control-router-endpoint'),
    externalSpotEndpoint: required(values, 'external-spot-endpoint'),
    spotRouterEndpoint: required(values, 'spot-router-endpoint'),
    spotPubEndpoint: required(values, 'spot-pub-endpoint'),
    clientSpotPubEndpoint: values.get('client-spot-pub-endpoint'),
    playAExternalSpotEndpoint: values.get('play-a-external-spot-endpoint'),
    externalClientEndpoint: values.get('external-client-endpoint'),
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
