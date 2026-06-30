export interface RegistryOptions {
  readonly rid: string;
  readonly httpUrl: string;
  readonly registryPubEndpoint: string;
  readonly registryRouterEndpoint: string;
  readonly evidenceFile?: string;
  readonly logDir: string;
}

export function parseRegistryOptions(args: readonly string[]): RegistryOptions {
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
  const rid = values.get('rid') ?? 'registry';
  process.env.ZLINK_E2E_RID = rid;
  return {
    rid,
    httpUrl: required(values, 'http-url'),
    registryPubEndpoint: required(values, 'registry-pub-endpoint'),
    registryRouterEndpoint: required(values, 'registry-router-endpoint'),
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
