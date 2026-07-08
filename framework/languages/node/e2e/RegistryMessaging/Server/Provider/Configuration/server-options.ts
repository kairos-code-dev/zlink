export interface ServerOptions {
  readonly role: string;
  readonly httpUrl: string;
  readonly logDir: string;
  readonly evidenceFile?: string;
  readonly rid: string;
  readonly redisEndpoint?: string;
  readonly redisKeyPrefix?: string;
  readonly channelEndpoint?: string;
  readonly manualClientEndpoint?: string;
  readonly routeEndpoint?: string;
  readonly routePeers: readonly string[];
  readonly weight: number;
  readonly maxMessageSize: number;
}

export function parseServerOptions(args: readonly string[], defaultRole = 'provider'): ServerOptions {
  const values = new Map<string, string>();
  const routePeers: string[] = [];
  for (let i = 0; i < args.length; i += 1) {
    const key = args[i];
    if (!key.startsWith('--')) {
      continue;
    }
    if (i + 1 >= args.length) {
      throw new Error(`Missing value for ${key}.`);
    }
    const value = args[++i];
    if (key === '--route-peer') {
      routePeers.push(value);
    } else {
      values.set(key.slice(2), value);
    }
  }

  const rid = values.get('rid') ?? 'node';
  process.env.ZLINK_E2E_RID = rid;
  return {
    role: defaultRole,
    httpUrl: values.get('http-url') ?? 'http://127.0.0.1:0',
    logDir: values.get('log-dir') ?? '/tmp/zlink-node-e2e-log',
    evidenceFile: values.get('evidence-file'),
    rid,
    redisEndpoint: values.get('redis-endpoint'),
    redisKeyPrefix: values.get('redis-key-prefix'),
    channelEndpoint: values.get('channel-endpoint'),
    manualClientEndpoint: values.get('manual-client-endpoint'),
    routeEndpoint: values.get('route-endpoint'),
    routePeers,
    weight: parseWeight(values.get('weight')),
    maxMessageSize: parseMaxMessageSize(values.get('max-message-size'))
  };
}

function parseWeight(value: string | undefined): number {
  if (value === undefined) {
    return 100;
  }
  const weight = Number(value);
  if (!Number.isInteger(weight) || weight < 0 || weight > 100) {
    throw new Error('--weight must be an integer between 0 and 100.');
  }
  return weight;
}

function parseMaxMessageSize(value: string | undefined): number {
  if (value === undefined) {
    return 0;
  }
  const size = Number(value);
  if (!Number.isInteger(size) || size < 0) {
    throw new Error('--max-message-size must be a non-negative integer.');
  }
  return size;
}
