export interface ServerOptions {
  readonly rid: string;
  readonly httpUrl: string;
  readonly logDir: string;
  readonly channelEndpoint: string;
  readonly evidenceFile?: string;
}

export function parseServerOptions(args: readonly string[], defaultRid = 'reg-codec-node'): ServerOptions {
  const values = parseArgs(args);
  return {
    rid: values.get('rid') ?? defaultRid,
    httpUrl: values.get('http-url') ?? 'http://127.0.0.1:0',
    logDir: values.get('log-dir') ?? 'logs',
    channelEndpoint: required(values, 'channel-endpoint'),
    evidenceFile: values.get('evidence-file')
  };
}

export function parseArgs(args: readonly string[]): Map<string, string> {
  const values = new Map<string, string>();
  for (let i = 0; i < args.length; i += 1) {
    const key = args[i];
    if (!key.startsWith('--') || i + 1 >= args.length) {
      throw new Error(`Invalid argument '${key}'.`);
    }
    values.set(key.slice(2), args[++i]);
  }
  return values;
}

export function required(values: Map<string, string>, name: string): string {
  const value = values.get(name);
  if (value === undefined || value.length === 0) {
    throw new Error(`--${name} is required.`);
  }
  return value;
}
