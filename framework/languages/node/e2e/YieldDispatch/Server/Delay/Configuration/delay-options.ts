export interface DelayOptions {
  readonly rid: string;
  readonly httpUrl: string;
  readonly delayEndpoint: string;
  readonly evidenceFile?: string;
}

export function parseDelayOptions(args: readonly string[]): DelayOptions {
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
    delayEndpoint: required(values, 'delay-endpoint'),
    evidenceFile: values.get('evidence-file')
  };
}

function required(values: ReadonlyMap<string, string>, key: string): string {
  const value = values.get(key);
  if (value === undefined || value.length === 0) {
    throw new Error(`--${key} is required.`);
  }
  return value;
}
