export interface ClientOptions {
  readonly serverUrl: string;
  readonly jsonOnlyUrl: string;
  readonly codecRequesterUrl: string;
  readonly invalidMain: string;
  readonly invalidDuplicateConfig: string;
  readonly invalidHandlerGroupConfig: string;
  readonly invalidChannelKindsConfig: string;
  readonly logDir: string;
  readonly scenario: string;
}

export function parseClientOptions(args: readonly string[]): ClientOptions {
  const values = new Map<string, string>();
  for (let i = 0; i < args.length; i += 1) {
    const key = args[i];
    if (!key.startsWith('--') || i + 1 >= args.length) {
      throw new Error(`Invalid argument '${key}'.`);
    }
    values.set(key.slice(2), args[++i]);
  }
  return {
    serverUrl: required(values, 'server-url'),
    jsonOnlyUrl: required(values, 'json-only-url'),
    codecRequesterUrl: required(values, 'codec-requester-url'),
    invalidMain: required(values, 'invalid-main'),
    invalidDuplicateConfig: required(values, 'invalid-duplicate-config'),
    invalidHandlerGroupConfig: required(values, 'invalid-handler-group-config'),
    invalidChannelKindsConfig: required(values, 'invalid-channel-kinds-config'),
    logDir: required(values, 'log-dir'),
    scenario: values.get('scenario') ?? 'all'
  };
}

function required(values: Map<string, string>, name: string): string {
  const value = values.get(name);
  if (value === undefined || value.length === 0) {
    throw new Error(`--${name} is required.`);
  }
  return value;
}
