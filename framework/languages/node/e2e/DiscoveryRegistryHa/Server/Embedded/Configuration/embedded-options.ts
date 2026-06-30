import { parseArgs, required } from '../../Registry/Configuration/registry-options';

export interface EmbeddedOptions {
  readonly rid: string;
  readonly httpUrl: string;
  readonly logDir: string;
  readonly registryId: number;
  readonly registryPubEndpoint: string;
  readonly registryRouterEndpoint: string;
  readonly channelEndpoint: string;
  readonly peers: readonly string[];
  readonly registryRouterEndpoints: readonly string[];
  readonly evidenceFile?: string;
}

export function parseEmbeddedOptions(args: readonly string[]): EmbeddedOptions {
  const values = parseArgs(args);
  const registryRouterEndpoint = required(values, 'registry-router-endpoint');
  const discoveryEndpoints = values.getAll('registry-router-endpoint');
  return {
    rid: values.get('rid') ?? 'embedded-api',
    httpUrl: values.get('http-url') ?? 'http://127.0.0.1:0',
    logDir: values.get('log-dir') ?? 'logs',
    registryId: Number.parseInt(values.get('registry-id') ?? '1', 10),
    registryPubEndpoint: required(values, 'registry-pub-endpoint'),
    registryRouterEndpoint,
    channelEndpoint: required(values, 'channel-endpoint'),
    peers: values.getAll('peer'),
    registryRouterEndpoints: discoveryEndpoints.length === 0 ? [registryRouterEndpoint] : discoveryEndpoints,
    evidenceFile: values.get('evidence-file')
  };
}
