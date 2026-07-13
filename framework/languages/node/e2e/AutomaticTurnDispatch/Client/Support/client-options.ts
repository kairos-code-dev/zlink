export interface ClientOptions {
  readonly sessionAStreamEndpoint: string;
  readonly sessionBStreamEndpoint: string;
  readonly scenario: string;
  readonly requestId?: string;
  readonly spotRid?: string;
}

export function parseClientOptions(args: readonly string[]): ClientOptions {
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
    sessionAStreamEndpoint: required(values, 'session-a-stream-endpoint'),
    sessionBStreamEndpoint: required(values, 'session-b-stream-endpoint'),
    scenario: values.get('scenario') ?? 'full',
    requestId: values.get('request-id'),
    spotRid: values.get('spot-rid')
  };
}

function required(values: ReadonlyMap<string, string>, key: string): string {
  const value = values.get(key);
  if (value === undefined || value.length === 0) {
    throw new Error(`--${key} is required.`);
  }
  return value;
}
