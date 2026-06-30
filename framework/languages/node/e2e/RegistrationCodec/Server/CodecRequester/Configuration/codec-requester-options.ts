export interface CodecRequesterOptions {
  readonly rid: string;
  readonly httpUrl: string;
  readonly logDir: string;
  readonly targetEndpoint: string;
}

export function parseCodecRequesterOptions(args: readonly string[]): CodecRequesterOptions {
  const values = new Map<string, string>();
  for (let i = 0; i < args.length; i += 1) {
    const key = args[i];
    if (!key.startsWith('--') || i + 1 >= args.length) {
      throw new Error(`Invalid argument '${key}'.`);
    }
    values.set(key.slice(2), args[++i]);
  }
  return {
    rid: values.get('rid') ?? 'codec-requester',
    httpUrl: required(values, 'http-url'),
    logDir: values.get('log-dir') ?? 'logs',
    targetEndpoint: required(values, 'target-endpoint')
  };
}

function required(values: Map<string, string>, name: string): string {
  const value = values.get(name);
  if (value === undefined || value.length === 0) {
    throw new Error(`--${name} is required.`);
  }
  return value;
}
