export interface ClientOptions {
  readonly actorUrl: string;
  readonly callerUrl: string;
  readonly sessionUrl: string;
  readonly sessionStreamEndpoint: string;
  readonly scenario: string;
}

export function parseClientOptions(args: readonly string[]): ClientOptions {
  const values = new Map<string, string>();
  for (let index = 0; index < args.length; index += 2) {
    const key = args[index]?.replace(/^--/, '');
    const value = args[index + 1];
    if (key === undefined || value === undefined) {
      throw new Error(`Missing value for '${args[index]}'.`);
    }
    values.set(key, value);
  }
  return {
    actorUrl: values.get('actor-url') ?? 'http://127.0.0.1:0',
    callerUrl: values.get('caller-url') ?? 'http://127.0.0.1:0',
    sessionUrl: values.get('session-url') ?? 'http://127.0.0.1:0',
    sessionStreamEndpoint: values.get('session-stream-endpoint') ?? 'ws://127.0.0.1:0',
    scenario: values.get('scenario') ?? 'all'
  };
}
