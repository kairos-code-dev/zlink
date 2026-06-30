export interface InvalidDuplicateOptions {
  readonly rid: string;
  readonly logDir: string;
  readonly channelEndpoint: string;
}

export function parseInvalidDuplicateOptions(args: readonly string[]): InvalidDuplicateOptions {
  const values = new Map<string, string>();
  for (let i = 0; i < args.length; i += 1) {
    const key = args[i];
    if (!key.startsWith('--') || i + 1 >= args.length) {
      throw new Error(`Invalid argument '${key}'.`);
    }
    values.set(key.slice(2), args[++i]);
  }
  return {
    rid: values.get('rid') ?? 'invalid-duplicate',
    logDir: values.get('log-dir') ?? 'logs',
    channelEndpoint: required(values, 'channel-endpoint')
  };
}

function required(values: Map<string, string>, name: string): string {
  const value = values.get(name);
  if (value === undefined || value.length === 0) {
    throw new Error(`--${name} is required.`);
  }
  return value;
}
