import { parseArgs, required } from '../../Server/Registry/Configuration/registry-options';

export interface ClientOptions {
  readonly registryUrl: string;
  readonly registry2Url?: string;
  readonly registry3Url?: string;
  readonly consumerUrl: string;
  readonly consumer2Url?: string;
  readonly consumer3Url?: string;
  readonly probeUrl?: string;
  readonly providerAUrl: string;
  readonly providerBUrl?: string;
  readonly providerCUrl?: string;
  readonly providerCEndpoint?: string;
  readonly embeddedUrl?: string;
  readonly embeddedConsumerUrl?: string;
  readonly duplicateProviderUrl?: string;
  readonly registryPubEndpoint?: string;
  readonly registry2PubEndpoint?: string;
  readonly registry3PubEndpoint?: string;
  readonly registryRouterEndpoint?: string;
  readonly registry2RouterEndpoint?: string;
  readonly registry3RouterEndpoint?: string;
  readonly logDir?: string;
  readonly registryMain?: string;
  readonly providerMain?: string;
  readonly consumerMain?: string;
  readonly scenario: string;
}

export function parseClientOptions(args: readonly string[]): ClientOptions {
  const values = parseArgs(args);
  return {
    registryUrl: required(values, 'registry-url'),
    registry2Url: values.get('registry-2-url'),
    registry3Url: values.get('registry-3-url'),
    consumerUrl: required(values, 'consumer-url'),
    consumer2Url: values.get('consumer-2-url'),
    consumer3Url: values.get('consumer-3-url'),
    probeUrl: values.get('probe-url'),
    providerAUrl: required(values, 'provider-a-url'),
    providerBUrl: values.get('provider-b-url'),
    providerCUrl: values.get('provider-c-url'),
    providerCEndpoint: values.get('provider-c-endpoint'),
    embeddedUrl: values.get('embedded-url'),
    embeddedConsumerUrl: values.get('embedded-consumer-url'),
    duplicateProviderUrl: values.get('duplicate-provider-url'),
    registryPubEndpoint: values.get('registry-pub-endpoint'),
    registry2PubEndpoint: values.get('registry-2-pub-endpoint'),
    registry3PubEndpoint: values.get('registry-3-pub-endpoint'),
    registryRouterEndpoint: values.get('registry-router-endpoint'),
    registry2RouterEndpoint: values.get('registry-2-router-endpoint'),
    registry3RouterEndpoint: values.get('registry-3-router-endpoint'),
    logDir: values.get('log-dir'),
    registryMain: values.get('registry-main'),
    providerMain: values.get('provider-main'),
    consumerMain: values.get('consumer-main'),
    scenario: values.get('scenario') ?? 'DR-A1'
  };
}
