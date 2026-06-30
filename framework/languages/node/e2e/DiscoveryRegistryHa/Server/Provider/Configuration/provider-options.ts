import { parseArgs, required } from '../../Registry/Configuration/registry-options';

export interface ProviderOptions {
  readonly rid: string;
  readonly httpUrl: string;
  readonly logDir: string;
  readonly registryRouterEndpoints: readonly string[];
  readonly channelEndpoint: string;
  readonly evidenceFile?: string;
}

export function parseProviderOptions(args: readonly string[]): ProviderOptions {
  const values = parseArgs(args);
  return {
    rid: values.get('rid') ?? 'api-a',
    httpUrl: values.get('http-url') ?? 'http://127.0.0.1:0',
    logDir: values.get('log-dir') ?? 'logs',
    registryRouterEndpoints: values.getAll('registry-router-endpoint').length === 0
      ? [required(values, 'registry-router-endpoint')]
      : values.getAll('registry-router-endpoint'),
    channelEndpoint: required(values, 'channel-endpoint'),
    evidenceFile: values.get('evidence-file')
  };
}
