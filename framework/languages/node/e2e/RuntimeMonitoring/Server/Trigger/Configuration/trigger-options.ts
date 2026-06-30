export interface TriggerOptions {
  readonly httpUrl: string;
  readonly serviceChannelEndpoint: string;
  readonly serviceBChannelEndpoint: string;
  readonly throwChannelEndpoint: string;
  readonly logDir: string;
}

export function parseTriggerOptions(args: readonly string[]): TriggerOptions {
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
  process.env.ZLINK_E2E_RID = 'trigger';
  return {
    httpUrl: required(values, 'http-url'),
    serviceChannelEndpoint: required(values, 'service-channel-endpoint'),
    serviceBChannelEndpoint: required(values, 'service-b-channel-endpoint'),
    throwChannelEndpoint: required(values, 'throw-channel-endpoint'),
    logDir: required(values, 'log-dir')
  };
}

function required(values: ReadonlyMap<string, string>, key: string): string {
  const value = values.get(key);
  if (value === undefined || value.length === 0) {
    throw new Error(`--${key} is required.`);
  }
  return value;
}
