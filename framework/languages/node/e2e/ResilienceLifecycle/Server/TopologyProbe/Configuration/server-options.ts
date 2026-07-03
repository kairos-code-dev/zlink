export interface ServerOptions {
  readonly role: string;
  readonly httpUrl: string;
  readonly logDir: string;
  readonly evidenceFile?: string;
  readonly rid: string;
  readonly redisEndpoint: string;
  readonly redisKeyPrefix: string;
}

export function parseServerOptions(args: readonly string[], defaultRole = 'topology-probe'): ServerOptions {
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
  const rid = values.get('rid') ?? 'node';
  const redisEndpoint = values.get('redis-endpoint');
  const redisKeyPrefix = values.get('redis-key-prefix');
  if (redisEndpoint === undefined) {
    throw new Error('--redis-endpoint is required.');
  }
  if (redisKeyPrefix === undefined) {
    throw new Error('--redis-key-prefix is required.');
  }
  process.env.ZLINK_E2E_RID = rid;
  return {
    role: defaultRole,
    httpUrl: values.get('http-url') ?? 'http://127.0.0.1:0',
    logDir: values.get('log-dir') ?? '/tmp/zlink-node-e2e-log',
    evidenceFile: values.get('evidence-file'),
    rid,
    redisEndpoint,
    redisKeyPrefix
  };
}
