export interface ServerOptions {
  readonly rid: string;
  readonly httpUrl: string;
  readonly redisEndpoint: string;
  readonly redisKeyPrefix: string;
  readonly routerEndpoint: string;
  readonly pubSubEndpoint: string;
  readonly streamEndpoint?: string;
  readonly evidenceFile?: string;
  readonly logDir: string;
}

export function parseServerOptions(args: readonly string[], role: string): ServerOptions {
  const values = parseArgs(args);
  return {
    rid: values.get('rid') ?? role,
    httpUrl: values.get('http-url') ?? 'http://127.0.0.1:0',
    redisEndpoint: values.get('redis-endpoint') ?? '127.0.0.1:6379',
    redisKeyPrefix: values.get('redis-key-prefix') ?? 'zlink:e2e:to-actor',
    routerEndpoint: values.get('router-endpoint') ?? 'tcp://127.0.0.1:0',
    pubSubEndpoint: values.get('pubsub-endpoint') ?? 'tcp://127.0.0.1:0',
    streamEndpoint: values.get('stream-endpoint'),
    evidenceFile: values.get('evidence-file'),
    logDir: values.get('log-dir') ?? 'logs'
  };
}

function parseArgs(args: readonly string[]): Map<string, string> {
  const values = new Map<string, string>();
  for (let index = 0; index < args.length; index += 2) {
    const key = args[index]?.replace(/^--/, '');
    const value = args[index + 1];
    if (key === undefined || value === undefined) {
      throw new Error(`Missing value for '${args[index]}'.`);
    }
    values.set(key, value);
  }
  return values;
}
