export interface ClientOptions {
  readonly topologyUrl: string;
  readonly consumerUrl: string;
  readonly providerAUrl: string;
  readonly providerBUrl?: string;
  readonly scenario: string;
}

export function parseClientOptions(args: readonly string[]): ClientOptions {
  const values = new Map<string, string>();
  for (let index = 0; index < args.length; index += 2) {
    const flag = args[index];
    const value = args[index + 1];
    if (!flag?.startsWith('--') || value === undefined) throw new Error(`Invalid client argument '${flag}'.`);
    values.set(flag.slice(2), value);
  }
  return {
    topologyUrl: required(values, 'topology-url'),
    consumerUrl: required(values, 'consumer-url'),
    providerAUrl: required(values, 'provider-a-url'),
    providerBUrl: values.get('provider-b-url'),
    scenario: values.get('scenario') ?? 'all'
  };
}

function required(values: ReadonlyMap<string, string>, name: string): string {
  const value = values.get(name);
  if (!value) throw new Error(`--${name} is required.`);
  return value;
}
