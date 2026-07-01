export interface ClientOptions {
  readonly playAUrl: string;
  readonly playBUrl: string;
  readonly gatewayUrl: string;
  readonly sessionAUrl: string;
  readonly sessionBUrl: string;
  readonly sessionAStreamEndpoint: string;
  readonly sessionATlsStreamEndpoint: string;
  readonly sessionBStreamEndpoint: string;
  readonly multiAUrl: string;
  readonly multiBUrl: string;
  readonly scenario: string;
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
    playAUrl: required(values, 'play-a-url'),
    playBUrl: required(values, 'play-b-url'),
    gatewayUrl: required(values, 'gateway-url'),
    sessionAUrl: required(values, 'session-a-url'),
    sessionBUrl: required(values, 'session-b-url'),
    sessionAStreamEndpoint: required(values, 'session-a-stream-endpoint'),
    sessionATlsStreamEndpoint: values.get('session-a-tls-stream-endpoint') ?? '',
    sessionBStreamEndpoint: required(values, 'session-b-stream-endpoint'),
    multiAUrl: values.get('multi-a-url') ?? 'http://127.0.0.1:0',
    multiBUrl: values.get('multi-b-url') ?? 'http://127.0.0.1:0',
    scenario: values.get('scenario') ?? 'all'
  };
}

function required(values: ReadonlyMap<string, string>, key: string): string {
  const value = values.get(key);
  if (value === undefined || value.length === 0) {
    throw new Error(`--${key} is required.`);
  }
  return value;
}
