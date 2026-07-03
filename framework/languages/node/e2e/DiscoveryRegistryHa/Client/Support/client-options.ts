import { parseArgs, required } from '../../Server/LocationProbe/Configuration/location-probe-options';

export interface ClientOptions {
  readonly topologyUrl: string;
  readonly consumerUrl: string;
  readonly providerAUrl: string;
  readonly providerBUrl?: string;
  readonly scenario: string;
}

export function parseClientOptions(args: readonly string[]): ClientOptions {
  const values = parseArgs(args);
  return {
    topologyUrl: required(values, 'topology-url'),
    consumerUrl: required(values, 'consumer-url'),
    providerAUrl: required(values, 'provider-a-url'),
    providerBUrl: values.get('provider-b-url'),
    scenario: values.get('scenario') ?? 'all'
  };
}
