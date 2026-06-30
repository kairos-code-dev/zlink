export interface ConsumerOptions {
  readonly httpUrl: string;
  readonly logDir: string;
  readonly traceLabel: string;
  readonly providerEndpoints: readonly string[];
  readonly registryRouterEndpoint?: string;
  readonly lowHighWaterMark: boolean;
}

export function parseConsumerOptions(args: readonly string[]): ConsumerOptions {
  const values = new Map<string, string[]>();
  for (let i = 0; i < args.length; i += 1) {
    const key = args[i];
    if (!key.startsWith('--')) {
      continue;
    }
    if (i + 1 >= args.length) {
      throw new Error(`Missing value for ${key}.`);
    }
    const name = key.slice(2);
    const list = values.get(name) ?? [];
    list.push(args[++i]);
    values.set(name, list);
  }
  const providerEndpoints = values.get('provider-endpoint') ?? [];
  const registryRouterEndpoint = last(values, 'registry-router-endpoint');
  if (providerEndpoints.length === 0 && registryRouterEndpoint === undefined) {
    throw new Error('--provider-endpoint or --registry-router-endpoint is required.');
  }
  return {
    httpUrl: last(values, 'http-url') ?? 'http://127.0.0.1:0',
    logDir: last(values, 'log-dir') ?? '/tmp/zlink-node-e2e-log',
    traceLabel: last(values, 'trace-label') ?? 'consumer',
    providerEndpoints,
    registryRouterEndpoint,
    lowHighWaterMark: (last(values, 'low-hwm') ?? 'false').toLowerCase() === 'true'
  };
}

function last(values: ReadonlyMap<string, readonly string[]>, key: string): string | undefined {
  const matched = values.get(key);
  return matched === undefined || matched.length === 0 ? undefined : matched[matched.length - 1];
}
